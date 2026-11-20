/* A network process as the client of a remote display session.

Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

/* This file is a transport.  It uses only the sink interface of
   remoteterm.h.  A session writes its messages to a sink of this
   file, and the sink sends them through a network connection.

   The session writes during redisplay, so the sink must not run Lisp
   and must not wait.  It writes to the file descriptor of the process
   with no wait.  The bytes that the connection cannot take now stay in
   a queue, and the process loop calls the sink again when the
   connection can take more.

   The client sends its input messages on the same connection.  The
   filter of the process, remote-display--receive, decodes them, and
   gives each one to remoteinput.c.

   When sessions end on purpose, remote-win.el ends their clients in
   three steps: remote-display--queue-end puts the end-session message
   in the queue of each client, remote-display--drain waits for all the
   queues with one deadline, and then each connection closes.  Thus
   each client gets the end before the close, and the knowledge that
   the session ended.  */

#include <config.h>

#include <errno.h>
#include <sys/socket.h>

#include "lisp.h"
#include "process.h"
#include "sysselect.h"
#include "systime.h"
#include "rdproto.h"
#include "remoteinput.h"
#include "remoteterm.h"

/* The input from a client that is not a complete message.  BYTES has
   USED such bytes, and has SIZE bytes.  SKIP is the number of bytes of
   a message of an unknown type that did not come.  After a message with
   an error, FAILED is true, and the input ignores all bytes.  */
struct remote_input
{
  unsigned char *bytes;
  ptrdiff_t used;
  ptrdiff_t size;
  ptrdiff_t skip;
  bool failed;
};

/* The data of a connection sink: the process, the session DPYINFO,
   the queue of the bytes that the connection did not take, and the
   input from the client.  The bytes from START to USED are not sent.
   WAITING is true while the process loop calls
   remote_connection_writable for the file descriptor.  NEXT is the next
   connection of remote_connections.  */
struct remote_connection
{
  Lisp_Object process;
  struct remote_display_info *dpyinfo;
  unsigned char *queue;
  ptrdiff_t start;
  ptrdiff_t used;
  ptrdiff_t size;
  bool waiting;
  struct remote_input input;
  struct remote_connection *next;
};

static struct remote_sink *remote_make_connection_sink
  (struct remote_display_info *, Lisp_Object);
static struct remote_connection *remote_find_connection (Lisp_Object);
static void remote_connection_receive (struct remote_connection *,
				       const unsigned char *, ptrdiff_t);
static void remote_connection_pong (struct remote_connection *,
				    const struct rd_ping *);
static void remote_connection_put (struct remote_connection *,
				   const struct rd_message *);
static int remote_drain_set (Lisp_Object, fd_set *);
static void remote_input_add (struct remote_input *, const unsigned char *,
			      ptrdiff_t);
static bool remote_input_next (struct remote_input *, struct rd_message *);
static void remote_connection_write (void *, const unsigned char *, ptrdiff_t);
static void remote_connection_flush (void *);
static void remote_connection_free (void *);
static ptrdiff_t remote_connection_queued (void *);
static void remote_connection_writable (int, void *);
static void remote_connection_send (struct remote_connection *);
static bool remote_would_block (int);
static int remote_connection_fd (struct remote_connection *);
static void remote_connection_stop_waiting (struct remote_connection *, int);
static void remote_check_connection (Lisp_Object);

static const struct remote_sink_ops remote_connection_ops =
  { remote_connection_write, remote_connection_flush,
    remote_connection_free, remote_connection_queued };

/* The processes of the connection sinks.  The sinks are not Lisp
   objects, so this list keeps their processes from the garbage
   collector.  The list also gives the rule that a process is the
   client of at most one session.  remote-display-attach adds the
   process of each new sink, and remote_connection_free removes it, so
   the list has exactly the processes of the live sinks.  */
static Lisp_Object remote_connection_processes;

/* The connections of the live sinks.  */
static struct remote_connection *remote_connections;

DEFUN ("remote-display-attach", Fremote_display_attach,
       Sremote_display_attach, 2, 2, 0,
       doc: /* Attach the network connection PROCESS to the session DISPLAY.
After this call, the session sends its messages to PROCESS.  The last
client of the session is detached.  PROCESS must be a live network
connection, and not a server.  A process can be the client of only
one session.  This function sets the filter and the sentinel of
PROCESS, and clears its query flag, so a quit of Emacs does not ask
about the client.  When PROCESS closes, the session has no client, and
keeps its frames.  */)
  (Lisp_Object display, Lisp_Object process)
{
  struct remote_display_info *dpyinfo = check_x_display_info (display);
  remote_check_connection (process);
  remote_connection_processes = Fcons (process, remote_connection_processes);
  remote_set_sink (dpyinfo, remote_make_connection_sink (dpyinfo, process));
  Fset_process_coding_system (process, Qno_conversion, Qno_conversion);
  Fset_process_filter (process, Qremote_display__receive);
  Fset_process_sentinel (process, Qremote_display__closed);
  Fset_process_query_on_exit_flag (process, Qnil);
  return Qnil;
}

DEFUN ("remote-display-client", Fremote_display_client,
       Sremote_display_client, 1, 1, 0,
       doc: /* Return the network connection that is the client of DISPLAY.
Return nil if no network connection is attached to the session
DISPLAY.  */)
  (Lisp_Object display)
{
  struct remote_connection *connection
    = remote_sink_data (check_x_display_info (display),
			&remote_connection_ops);
  return connection ? connection->process : Qnil;
}

DEFUN ("remote-display--queue-end", Fremote_display__queue_end,
       Sremote_display__queue_end, 1, 1, 0,
       doc: /* Put the end of the session DISPLAY in the queue of its client.
The client gets an end-session message after all the messages before
it.  The connection sends the bytes that it can take now, and keeps
the others in its queue: see `remote-display--drain'.  Return the
network connection of the client, or nil if DISPLAY has no client.  */)
  (Lisp_Object display)
{
  struct remote_connection *connection
    = remote_sink_data (check_x_display_info (display),
			&remote_connection_ops);
  if (!connection)
    return Qnil;
  struct rd_message end = rd_end_session_message (RD_END_CLOSED);
  remote_connection_put (connection, &end);
  remote_connection_send (connection);
  return connection->process;
}

DEFUN ("remote-display--drain", Fremote_display__drain,
       Sremote_display__drain, 2, 2, 0,
       doc: /* Wait until each client of CLIENTS sent its queue.
CLIENTS is a list of the network connections of clients.  Wait at most
LIMIT seconds in all, for all the clients together.  The wait runs no
Lisp, so no input of a client changes its session.  A connection that
closes or fails discards its queue, so the wait for it stops then.
Return nil.  */)
  (Lisp_Object clients, Lisp_Object limit)
{
  CHECK_LIST (clients);
  struct timespec end = timespec_add (current_timespec (),
				      dtotimespec (extract_float (limit)));
  for (;;)
    {
      fd_set writable;
      int last = remote_drain_set (clients, &writable);
      struct timespec left = timespec_sub (end, current_timespec ());
      if (last < 0 || timespec_sign (left) <= 0)
	return Qnil;
      pselect (last + 1, NULL, &writable, NULL, &left, NULL);
    }
}

DEFUN ("remote-display--closed", Fremote_display__closed,
       Sremote_display__closed, 2, 2, 0,
       doc: /* Detach the client PROCESS from its session if PROCESS is closed.
`remote-display-attach' makes this function the sentinel of PROCESS.
EVENT is the change of the status of PROCESS.  The session keeps its
terminal and its frames.  */)
  (Lisp_Object process, Lisp_Object event)
{
  struct remote_connection *connection = remote_find_connection (process);
  if (connection && remote_connection_fd (connection) < 0)
    remote_set_sink (connection->dpyinfo, NULL);
  return Qnil;
}

DEFUN ("remote-display--receive", Fremote_display__receive,
       Sremote_display__receive, 2, 2, 0,
       doc: /* Give the bytes STRING from the client PROCESS to its session.
`remote-display-attach' makes this function the filter of PROCESS.  */)
  (Lisp_Object process, Lisp_Object string)
{
  CHECK_STRING (string);
  struct remote_connection *connection = remote_find_connection (process);
  if (connection)
    remote_connection_receive (connection, SDATA (string), SBYTES (string));
  return Qnil;
}

/* Return the connection of the sink of PROCESS, or NULL if PROCESS is
   not the client of a session.  */
static struct remote_connection *
remote_find_connection (Lisp_Object process)
{
  for (struct remote_connection *c = remote_connections; c; c = c->next)
    if (EQ (c->process, process))
      return c;
  return NULL;
}

/* Give the SIZE BYTES from the client to the session of CONNECTION:
   give each message that is complete now to remote_handle_input.  The
   handler can quit, for example for the key C-g, so the input removes
   each message before the handler gets it.  */
static void
remote_connection_receive (struct remote_connection *connection,
			   const unsigned char *bytes, ptrdiff_t size)
{
  remote_input_add (&connection->input, bytes, size);
  struct rd_message message;
  while (remote_input_next (&connection->input, &message))
    if (message.type == RD_PING)
      remote_connection_pong (connection, &message.u.ping);
    else
      remote_handle_input (connection->dpyinfo, &message);
}

/* Answer the PING of the client of CONNECTION with a pong of the same
   serial.  A ping is a matter of the connection and not of the
   session, so it does not go to remoteinput.c.  */
static void
remote_connection_pong (struct remote_connection *connection,
			const struct rd_ping *ping)
{
  struct rd_message pong = { .type = RD_PONG,
			     .u.pong = { .serial = ping->serial } };
  remote_connection_put (connection, &pong);
  remote_connection_send (connection);
}

/* Add MESSAGE to the queue of CONNECTION.  */
static void
remote_connection_put (struct remote_connection *connection,
		       const struct rd_message *message)
{
  USE_SAFE_ALLOCA;
  ptrdiff_t size = rd_encoded_size (message);
  unsigned char *bytes = SAFE_ALLOCA (size);
  rd_encode (message, bytes);
  remote_connection_write (connection, bytes, size);
  SAFE_FREE ();
}

/* Send the queue of each connection of the processes CLIENTS that it
   can take now.  Put the descriptor of each connection that still has
   bytes in WRITABLE.  Return the largest such descriptor, or -1 if no
   connection has bytes.  An element of CLIENTS that is not the client
   of a session has no bytes.  */
static int
remote_drain_set (Lisp_Object clients, fd_set *writable)
{
  int last = -1;
  FD_ZERO (writable);
  FOR_EACH_TAIL (clients)
    {
      struct remote_connection *connection
	= remote_find_connection (XCAR (clients));
      if (!connection)
	continue;
      remote_connection_send (connection);
      int fd = remote_connection_fd (connection);
      if (fd >= 0 && remote_connection_queued (connection) > 0)
	{
	  FD_SET (fd, writable);
	  last = max (last, fd);
	}
    }
  return last;
}

/* Add the SIZE BYTES to INPUT, after the bytes of an unknown message
   that INPUT skips.  After an error, ignore them.  */
static void
remote_input_add (struct remote_input *input, const unsigned char *bytes,
		  ptrdiff_t size)
{
  if (input->failed)
    return;
  ptrdiff_t skipped = min (size, input->skip);
  input->skip -= skipped;
  bytes += skipped;
  size -= skipped;
  ptrdiff_t needed = input->used + size - input->size;
  if (needed > 0)
    input->bytes = xpalloc (input->bytes, &input->size, needed, -1, 1);
  memcpy (input->bytes + input->used, bytes, size);
  input->used += size;
}

/* Remove the first complete message from INPUT, and set *MESSAGE to
   it.  Return false if INPUT has no complete message.  Skip a message
   of an unknown type.  After a message with an error, discard the
   input, and ignore all later bytes.  */
static bool
remote_input_next (struct remote_input *input, struct rd_message *message)
{
  for (;;)
    {
      size_t used;
      ptrdiff_t left = input->used;
      enum rd_status status = rd_decode (input->bytes, left, message, &used);
      if (status == RD_INCOMPLETE)
	return false;
      if (status != RD_OK && status != RD_UNKNOWN_TYPE)
	{
	  input->failed = true;
	  input->used = 0;
	  return false;
	}
      if (status == RD_UNKNOWN_TYPE && used > left)
	{
	  input->skip = used - left;
	  used = left;
	}
      memmove (input->bytes, input->bytes + used, left - used);
      input->used = left - used;
      if (status == RD_OK)
	return true;
    }
}

/* Signal an error if PROCESS cannot be the client of a session.  */
static void
remote_check_connection (Lisp_Object process)
{
  CHECK_PROCESS (process);
  struct Lisp_Process *p = XPROCESS (process);
  if (!EQ (p->type, Qnetwork) || p->outfd < 0 || p->is_server)
    error ("The process is not a live network connection");
#ifdef HAVE_GNUTLS
  if (p->gnutls_p)
    error ("A TLS connection cannot be the client of a session");
#endif
  if (!NILP (Fmemq (process, remote_connection_processes)))
    error ("The process is already the client of a session");
}

/* Return a new sink of the session DPYINFO that sends its bytes to
   PROCESS.  */
static struct remote_sink *
remote_make_connection_sink (struct remote_display_info *dpyinfo,
			     Lisp_Object process)
{
  struct remote_connection *connection = xzalloc (sizeof *connection);
  connection->process = process;
  connection->dpyinfo = dpyinfo;
  connection->next = remote_connections;
  remote_connections = connection;
  return remote_make_sink (&remote_connection_ops, connection);
}

/* The write function of a connection sink.  It adds the bytes to the
   queue.  */
static void
remote_connection_write (void *data, const unsigned char *bytes,
			 ptrdiff_t size)
{
  struct remote_connection *connection = data;
  ptrdiff_t needed = connection->used + size - connection->size;
  if (needed > 0)
    connection->queue = xpalloc (connection->queue, &connection->size,
				 needed, -1, 1);
  memcpy (connection->queue + connection->used, bytes, size);
  connection->used += size;
}

/* The flush function of a connection sink.  */
static void
remote_connection_flush (void *data)
{
  remote_connection_send (data);
}

/* The queued function of a connection sink.  */
static ptrdiff_t
remote_connection_queued (void *data)
{
  struct remote_connection *connection = data;
  return connection->used - connection->start;
}

/* The process loop calls this function when the file descriptor of
   the connection DATA can take more bytes.  Tell the session when the
   queue is empty.  */
static void
remote_connection_writable (int fd, void *data)
{
  struct remote_connection *connection = data;
  remote_connection_send (connection);
  if (remote_connection_queued (connection) == 0)
    remote_output_drained (connection->dpyinfo);
}

/* The free function of a connection sink.  */
static void
remote_connection_free (void *data)
{
  struct remote_connection *connection = data;
  remote_connection_stop_waiting (connection,
				  remote_connection_fd (connection));
  remote_connection_processes = Fdelq (connection->process,
				       remote_connection_processes);
  struct remote_connection **link = &remote_connections;
  while (*link != connection)
    link = &(*link)->next;
  *link = connection->next;
  xfree (connection->input.bytes);
  xfree (connection->queue);
  xfree (connection);
}

/* Send the bytes of the queue of CONNECTION that the connection can
   take now.  If bytes stay, wait until the connection can take more.
   If the connection is closed, or a write fails, discard the queue.  */
static void
remote_connection_send (struct remote_connection *connection)
{
  int fd = remote_connection_fd (connection);
  while (fd >= 0 && connection->start < connection->used)
    {
      ssize_t sent = send (fd, connection->queue + connection->start,
			   connection->used - connection->start,
			   MSG_NOSIGNAL | MSG_DONTWAIT);
      if (sent >= 0)
	connection->start += sent;
      else if (remote_would_block (errno))
	{
	  connection->waiting = true;
	  add_write_fd (fd, remote_connection_writable, connection);
	  return;
	}
      else if (errno != EINTR)
	break;
    }
  connection->start = connection->used = 0;
  remote_connection_stop_waiting (connection, fd);
}

/* Return true if the error number ERR means that a write must wait.  */
static bool
remote_would_block (int err)
{
#if EWOULDBLOCK != EAGAIN
  if (err == EWOULDBLOCK)
    return true;
#endif
  return err == EAGAIN;
}

/* Return the file descriptor of the process of CONNECTION, or -1 if
   the process has no open connection.  A closed descriptor can come
   back for another file, so this function does not keep it.  */
static int
remote_connection_fd (struct remote_connection *connection)
{
  return XPROCESS (connection->process)->outfd;
}

/* Tell the process loop to stop the calls for CONNECTION, if it makes
   them.  FD is the file descriptor of the connection, or -1 if it is
   closed: the close also stopped the calls.  A call of add_write_fd
   with no function is necessary, because delete_write_fd keeps the
   function of a descriptor that a process also reads.  */
static void
remote_connection_stop_waiting (struct remote_connection *connection, int fd)
{
  if (connection->waiting && fd >= 0)
    {
      add_write_fd (fd, NULL, NULL);
      delete_write_fd (fd);
    }
  connection->waiting = false;
}

void
syms_of_remoteproc (void)
{
  remote_connection_processes = Qnil;
  staticpro (&remote_connection_processes);

  DEFSYM (Qremote_display__receive, "remote-display--receive");
  DEFSYM (Qremote_display__closed, "remote-display--closed");
  defsubr (&Sremote_display_attach);
  defsubr (&Sremote_display_client);
  defsubr (&Sremote_display__receive);
  defsubr (&Sremote_display__queue_end);
  defsubr (&Sremote_display__drain);
  defsubr (&Sremote_display__closed);
}
