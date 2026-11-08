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
   connection can take more.  */

#include <config.h>

#include <errno.h>
#include <sys/socket.h>

#include "lisp.h"
#include "process.h"
#include "remoteterm.h"

/* The data of a connection sink: the process, and the queue of the
   bytes that the connection did not take.  The bytes from START to
   USED are not sent.  WAITING is true while the process loop calls
   remote_connection_writable for the file descriptor.  */
struct remote_connection
{
  Lisp_Object process;
  unsigned char *queue;
  ptrdiff_t start;
  ptrdiff_t used;
  ptrdiff_t size;
  bool waiting;
};

static struct remote_sink *remote_make_connection_sink (Lisp_Object);
static void remote_connection_write (void *, const unsigned char *, ptrdiff_t);
static void remote_connection_flush (void *);
static void remote_connection_free (void *);
static void remote_connection_writable (int, void *);
static void remote_connection_send (struct remote_connection *);
static bool remote_would_block (int);
static int remote_connection_fd (struct remote_connection *);
static void remote_connection_stop_waiting (struct remote_connection *, int);
static void remote_check_connection (Lisp_Object);

static const struct remote_sink_ops remote_connection_ops =
  { remote_connection_write, remote_connection_flush,
    remote_connection_free };

/* The processes of the connection sinks.  The sinks are not Lisp
   objects, so this list keeps their processes from the garbage
   collector.  The list also gives the rule that a process is the
   client of at most one session.  remote-display-attach adds the
   process of each new sink, and remote_connection_free removes it, so
   the list has exactly the processes of the live sinks.  */
static Lisp_Object remote_connection_processes;

DEFUN ("remote-display-attach", Fremote_display_attach,
       Sremote_display_attach, 2, 2, 0,
       doc: /* Attach the network connection PROCESS to the session DISPLAY.
After this call, the session sends its messages to PROCESS.  The last
client of the session is detached.  PROCESS must be a live network
connection, and not a server.  A process can be the client of only
one session.  */)
  (Lisp_Object display, Lisp_Object process)
{
  struct remote_display_info *dpyinfo = check_x_display_info (display);
  remote_check_connection (process);
  remote_connection_processes = Fcons (process, remote_connection_processes);
  remote_set_sink (dpyinfo, remote_make_connection_sink (process));
  return Qnil;
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

/* Return a new sink that sends its bytes to PROCESS.  */
static struct remote_sink *
remote_make_connection_sink (Lisp_Object process)
{
  struct remote_connection *connection = xzalloc (sizeof *connection);
  connection->process = process;
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

/* The process loop calls this function when the file descriptor of
   the connection DATA can take more bytes.  */
static void
remote_connection_writable (int fd, void *data)
{
  remote_connection_send (data);
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

  defsubr (&Sremote_display_attach);
}
