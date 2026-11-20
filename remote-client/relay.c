/* The relay of remote-client.

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

#include <config.h>

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "clock.h"
#include "flow.h"
#include "listenspec.h"
#include "pending.h"
#include "rdproto.h"
#include "relay.h"
#include "relayline.h"
#include "stream.h"
#include "tls.h"
#include "token.h"
#include "unixsocket.h"

/* The state of a relay.  KEY is the TLS key.  PENDING holds the
   sockets of the connections that wait for their token, and WAITING
   their streams, or NULL.  CLIENT and SERVER are the connected client
   and its connection to the session, or NULL.  UP holds the bytes of
   the client for the session, and DOWN the bytes of the session for
   the client.  REFUSED is a client that the relay refuses because it
   cannot connect to the session, or NULL.  BYE holds the bytes of the
   end-session message that REFUSED did not get yet.  At BYE_DEADLINE
   of rc_now_ms, the relay closes REFUSED.  After a refusal, STOPPING
   is true, and the relay exits when it has no refused client.  */
struct relay
{
  const char *session;
  uint8_t token[RC_TOKEN_SIZE];
  struct rc_tls_key *key;
  int listener;
  struct rc_pending_table pending;
  struct rc_stream *waiting[RC_PENDING_SIZE];
  struct rc_stream *client, *server;
  struct rc_flow up, down;
  struct rc_stream *refused;
  struct rc_flow bye;
  int64_t bye_deadline;
  bool stopping;
};

/* The process after start_daemon.  */
enum daemon
  {
    /* The process that called start_daemon.  */
    DAEMON_PARENT,
    /* The new process of the relay.  */
    DAEMON_CHILD,
    /* No new process started.  */
    DAEMON_FAILED,
  };

/* The most connections that the kernel holds before accept_client
   takes them.  */
enum { BACKLOG = 16 };

/* The longest time of a refusal: the relay sends the end of the
   session to the client, and waits for the close of the client, for at
   most this number of milliseconds.  */
enum { END_LIMIT = 1000 };

/* The positions of the descriptors in the array of poll.  */
enum
  {
    WATCH_LISTENER,
    WATCH_CLIENT,
    WATCH_SERVER,
    WATCH_REFUSED,
    WATCH_PENDING,
    WATCH_SIZE = WATCH_PENDING + RC_PENDING_SIZE,
  };

static bool prepare (struct relay *, const char *,
		     const struct rc_listen_spec *, struct rc_relay_line *);
static enum daemon start_daemon (void);
static void announce (const struct rc_relay_line *);
static void serve (struct relay *);
static bool stopped_p (const struct relay *);
static int watch (const struct relay *, struct pollfd *);
static void watch_pair (const struct relay *, struct pollfd *);
static bool client_buffered_p (const struct relay *);
static bool move_pair (struct relay *, const struct pollfd *, bool);
static void read_tokens (struct relay *, const struct pollfd *);
static void accept_client (struct relay *);
static void expire_pending (struct relay *);
static enum rc_pending_result read_token (struct relay *, int);
static void start_waiting (struct relay *, struct rc_stream *);
static struct rc_stream *stop_waiting (struct relay *, int);
static struct rc_stream *next_expired (struct relay *, int64_t);
static struct rc_stream *waiting_stream (const struct relay *, int);
static struct rc_stream *take_waiting (struct relay *, int);
static void close_all (struct relay *);
static void open_pair (struct relay *, struct rc_stream *);
static void close_pair (struct relay *);
static void refuse_client (struct relay *, struct rc_stream *);
static void watch_refused (const struct relay *, struct pollfd *);
static int64_t refusal_wait (const struct relay *, int64_t);
static void move_refused (struct relay *, const struct pollfd *);
static bool ended_p (struct rc_stream *);
static void end_refusal (struct relay *);
static bool move (struct rc_flow *, struct rc_stream *, short,
		  struct rc_stream *, short);
static bool fill_flow (struct rc_flow *, struct rc_stream *);
static bool drain_flow (struct rc_flow *, struct rc_stream *);
static bool session_socket_p (const char *);
static int listen_in_range (const struct rc_listen_spec *, uint16_t *);
static int listen_on (const struct rc_listen_spec *, uint16_t);

int
rc_relay_run (const char *session, const struct rc_listen_spec *spec)
{
  struct relay relay;
  struct rc_relay_line line;
  if (!prepare (&relay, session, spec, &line))
    return 1;
  switch (start_daemon ())
    {
    case DAEMON_PARENT:
      rc_tls_key_free (relay.key);
      return 0;
    case DAEMON_FAILED:
      rc_tls_key_free (relay.key);
      return 1;
    case DAEMON_CHILD:
      announce (&line);
      serve (&relay);
      close_all (&relay);
      return 0;
    }
  return 1;
}

/* Check the session socket SESSION, make the token, and listen at the
   place of SPEC.  Put the state in RELAY and the line in LINE.  Return
   false on an error.  */
static bool
prepare (struct relay *relay, const char *session,
	 const struct rc_listen_spec *spec, struct rc_relay_line *line)
{
  if (!session_socket_p (session))
    {
      fprintf (stderr, "remote-client: %s: not a socket\n", session);
      return false;
    }
  if (!rc_token_new (line->token))
    {
      fprintf (stderr, "remote-client: no random bytes for the token\n");
      return false;
    }
  relay->key = rc_tls_key_new ();
  if (!relay->key)
    {
      fprintf (stderr, "remote-client: no TLS key\n");
      return false;
    }
  rc_tls_key_pin (relay->key, line->pin);
  relay->listener = listen_in_range (spec, &line->port);
  if (relay->listener < 0)
    {
      rc_tls_key_free (relay->key);
      return false;
    }
  relay->session = session;
  memcpy (relay->token, line->token, sizeof relay->token);
  rc_pending_init (&relay->pending);
  memset (relay->waiting, 0, sizeof relay->waiting);
  relay->client = relay->server = NULL;
  relay->refused = NULL;
  relay->stopping = false;
  return true;
}

/* Start the process of the relay in a new session, so the end of SSH
   does not stop it.  */
static enum daemon
start_daemon (void)
{
  pid_t pid = fork ();
  if (pid < 0)
    {
      perror ("remote-client: fork");
      return DAEMON_FAILED;
    }
  if (pid > 0)
    return DAEMON_PARENT;
  setsid ();
  return DAEMON_CHILD;
}

/* Write LINE to the standard output, and then connect the standard
   streams to /dev/null, so SSH can stop.  Exit if the line cannot go
   out, because then no client can connect.  */
static void
announce (const struct rc_relay_line *line)
{
  char text[RC_RELAY_LINE_SIZE];
  rc_relay_line_format (line, text);
  if (fputs (text, stdout) < 0 || fflush (stdout) != 0)
    _exit (1);
  int null = open ("/dev/null", O_RDWR);
  if (null < 0)
    _exit (1);
  dup2 (null, STDIN_FILENO);
  dup2 (null, STDOUT_FILENO);
  dup2 (null, STDERR_FILENO);
  if (null > STDERR_FILENO)
    close (null);
}

/* Serve the clients of RELAY until the session stops.  After a
   refusal, serve until the relay has no refused client and no
   connection that waits for its token (stopped_p).  */
static void
serve (struct relay *relay)
{
  while (!stopped_p (relay))
    {
      struct pollfd fds[WATCH_SIZE];
      int wait = watch (relay, fds);
      if (poll (fds, WATCH_SIZE, wait) < 0)
	{
	  if (errno == EINTR)
	    continue;
	  return;
	}
      /* The pair goes first: a token can replace the pair, and then
	 the events of the old pair are no longer valid.  */
      if (!move_pair (relay, fds, client_buffered_p (relay)))
	return;
      /* The refused client goes before the tokens: a token can
	 replace the refused client.  */
      move_refused (relay, fds);
      read_tokens (relay, fds);
      if (fds[WATCH_LISTENER].revents)
	accept_client (relay);
      expire_pending (relay);
    }
}

/* Return true if RELAY must stop after a refusal: it has no refused
   client, and no connection waits for its token.  A connection that
   waits can be a client of the stopped session, and that client must
   get its own refusal.  */
static bool
stopped_p (const struct relay *relay)
{
  return (relay->stopping && !relay->refused
	  && rc_pending_count (&relay->pending) == 0);
}

/* Fill FDS with the descriptors of RELAY and their events.  Return the
   wait of poll in milliseconds: 0 if the client has buffered bytes to
   read, or else the time to the first deadline of a pending connection
   or of the refused client, or -1 for no limit.  */
static int
watch (const struct relay *relay, struct pollfd *fds)
{
  fds[WATCH_LISTENER] = (struct pollfd) { relay->listener, POLLIN, 0 };
  watch_pair (relay, fds);
  watch_refused (relay, fds);
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    {
      int fd = rc_pending_fd (&relay->pending, i);
      short events
	= (fd < 0 ? 0
	   : rc_stream_events (waiting_stream (relay, fd), POLLIN));
      fds[WATCH_PENDING + i] = (struct pollfd) { fd, events, 0 };
    }
  int64_t now = rc_now_ms ();
  int64_t wait = rc_pending_wait (&relay->pending, now);
  assert (wait <= RC_RELAY_TOKEN_LIMIT);
  return client_buffered_p (relay) ? 0 : refusal_wait (relay, wait);
}

/* Return true if the client of RELAY holds bytes that poll does not
   show, and the relay can read them now.  A pending stream needs no
   such check: its read asks only for the rest of the token, so the
   bytes after the token stay for the client.  */
static bool
client_buffered_p (const struct relay *relay)
{
  return (relay->client && rc_flow_empty_p (&relay->up)
	  && rc_stream_buffered_p (relay->client));
}

/* Fill the entries of the pair of RELAY in FDS.  A direction with
   bytes waits until its destination can take them.  A direction with
   no bytes waits for its source.  A descriptor with no event is -1,
   so poll does not report its end again and again.  */
static void
watch_pair (const struct relay *relay, struct pollfd *fds)
{
  short client = 0, server = 0;
  if (relay->client)
    {
      if (rc_flow_empty_p (&relay->up))
	client |= POLLIN;
      else
	server |= POLLOUT;
      if (rc_flow_empty_p (&relay->down))
	server |= POLLIN;
      else
	client |= POLLOUT;
    }
  fds[WATCH_CLIENT]
    = (struct pollfd) { client ? rc_stream_fd (relay->client) : -1,
			client, 0 };
  fds[WATCH_SERVER]
    = (struct pollfd) { server ? rc_stream_fd (relay->server) : -1,
			server, 0 };
}

/* Move the bytes of the pair of RELAY after the events of FDS.  Close
   the pair when the client or the session closes its connection.  A
   direction reads only when it has no bytes.  A direction toward a
   socket peer that closed drops its bytes (drain_flow), so the other
   direction still gives the last bytes of that peer, and then its
   end closes the pair.  Return false when the relay
   must stop: after a close, if the session socket did not exist
   before the close.  A session that ends removes its socket before it
   closes the connection of its client.  */
static bool
move_pair (struct relay *relay, const struct pollfd *fds, bool buffered)
{
  if (!relay->client)
    return true;
  short client = fds[WATCH_CLIENT].revents | (buffered ? POLLIN : 0);
  short server = fds[WATCH_SERVER].revents;
  if (move (&relay->up, relay->client, client, relay->server, server)
      && move (&relay->down, relay->server, server, relay->client, client))
    return true;
  bool session = session_socket_p (relay->session);
  close_pair (relay);
  return session;
}

/* Read the tokens of the pending connections of RELAY after the events
   of FDS.  Remove each connection with a complete token or an end from
   the table, and make a connection with the token the client.  */
static void
read_tokens (struct relay *relay, const struct pollfd *fds)
{
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    {
      int fd = fds[WATCH_PENDING + i].fd;
      if (fd < 0 || !fds[WATCH_PENDING + i].revents)
	continue;
      enum rc_pending_result result = read_token (relay, fd);
      if (result == RC_PENDING_MORE)
	continue;
      struct rc_stream *stream = stop_waiting (relay, fd);
      if (result == RC_PENDING_BAD)
	rc_stream_close (stream);
      else
	open_pair (relay, stream);
    }
}

/* Accept a new connection of RELAY, start its TLS session, and make it
   wait for its token.  Close the connection that the table removes,
   if any.  */
static void
accept_client (struct relay *relay)
{
  int fd = accept (relay->listener, NULL, NULL);
  if (fd < 0)
    return;
  fcntl (fd, F_SETFL, O_NONBLOCK);
  struct rc_stream *stream = rc_tls_accept (relay->key, fd);
  if (!stream)
    return;
  start_waiting (relay, stream);
}

/* Close each pending connection of RELAY whose deadline has passed.  */
static void
expire_pending (struct relay *relay)
{
  int64_t now = rc_now_ms ();
  for (struct rc_stream *stream; (stream = next_expired (relay, now));)
    rc_stream_close (stream);
}

/* Read the next bytes of the token of the pending connection FD of
   RELAY, and return the result.  The end of the connection is a wrong
   token.  */
static enum rc_pending_result
read_token (struct relay *relay, int fd)
{
  struct rc_pending *slot = rc_pending_find (&relay->pending, fd);
  struct rc_stream *stream = waiting_stream (relay, fd);
  assert (slot);
  size_t size;
  uint8_t *space = rc_pending_space (slot, &size);
  ptrdiff_t count = rc_stream_read (stream, space, size);
  if (count == RC_STREAM_WAIT)
    return RC_PENDING_MORE;
  return (count > 0 ? rc_pending_fill (slot, count, relay->token)
	  : RC_PENDING_BAD);
}

/* The table PENDING and the array WAITING of a relay hold the same
   connections: the table holds the socket and the token, and the
   array the stream.  Only the functions below change them, so they
   stay the same.  */

/* Make STREAM wait for its token in RELAY.  If the table is full,
   close the stream that the table removes.  */
static void
start_waiting (struct relay *relay, struct rc_stream *stream)
{
  int removed = rc_pending_add (&relay->pending, rc_stream_fd (stream),
				rc_now_ms () + RC_RELAY_TOKEN_LIMIT);
  if (removed >= 0)
    rc_stream_close (take_waiting (relay, removed));
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    if (!relay->waiting[i])
      {
	relay->waiting[i] = stream;
	return;
      }
  assert (!"no free entry for a waiting stream");
}

/* Stop the wait of the connection with the socket FD in RELAY, and
   return its stream.  */
static struct rc_stream *
stop_waiting (struct relay *relay, int fd)
{
  rc_pending_remove (rc_pending_find (&relay->pending, fd));
  return take_waiting (relay, fd);
}

/* Stop the wait of a connection of RELAY whose deadline is before NOW,
   and return its stream, or NULL if there is none.  */
static struct rc_stream *
next_expired (struct relay *relay, int64_t now)
{
  int fd = rc_pending_expired (&relay->pending, now);
  return fd < 0 ? NULL : take_waiting (relay, fd);
}

/* Return the waiting stream of the socket FD in RELAY.  */
static struct rc_stream *
waiting_stream (const struct relay *relay, int fd)
{
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    if (relay->waiting[i] && rc_stream_fd (relay->waiting[i]) == fd)
      return relay->waiting[i];
  assert (!"no waiting stream for the socket");
  return NULL;
}

/* Remove the waiting stream of the socket FD from the array of RELAY,
   and return it.  */
static struct rc_stream *
take_waiting (struct relay *relay, int fd)
{
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    if (relay->waiting[i] && rc_stream_fd (relay->waiting[i]) == fd)
      {
	struct rc_stream *stream = relay->waiting[i];
	relay->waiting[i] = NULL;
	return stream;
      }
  assert (!"no waiting stream for the socket");
  return NULL;
}

/* Close each stream of RELAY, and free its key.  */
static void
close_all (struct relay *relay)
{
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    rc_stream_close (relay->waiting[i]);
  close_pair (relay);
  end_refusal (relay);
  close (relay->listener);
  rc_tls_key_free (relay->key);
}

/* Make the stream CLIENT the client of RELAY, and connect it to the
   session.  Close the old pair.  If the relay cannot connect to the
   session, the session has stopped: refuse CLIENT, and stop the
   relay after the refusal.  A pair that opens cancels that stop.  */
static void
open_pair (struct relay *relay, struct rc_stream *client)
{
  close_pair (relay);
  int server = rc_connect_unix (relay->session, false);
  if (server < 0)
    {
      refuse_client (relay, client);
      return;
    }
  fcntl (server, F_SETFL, O_NONBLOCK);
  relay->client = client;
  relay->server = rc_stream_socket (server);
  if (!relay->server)
    {
      close_pair (relay);
      return;
    }
  rc_flow_init (&relay->up);
  rc_flow_init (&relay->down);
  /* The session works again, so the relay does not stop after a
     refusal.  */
  relay->stopping = false;
}

/* Close the client of RELAY and its connection to the session.  */
static void
close_pair (struct relay *relay)
{
  rc_stream_close (relay->client);
  rc_stream_close (relay->server);
  relay->client = relay->server = NULL;
}

/* The refusal of a client: the relay tells the client that the
   session does not exist, so the client does not connect again.  The
   client closes its side after the message.  The relay waits for that
   close before its own close, because a close with bytes of the client
   unread can make the client lose the message.  The poll loop of the
   relay runs the refusal, so the relay serves the other connections
   meanwhile.  */

/* Make CLIENT the refused client of RELAY, put the end-session message
   in the flow BYE, and stop the relay after the refusal.  Close the
   refused client before CLIENT, if any.  */
static void
refuse_client (struct relay *relay, struct rc_stream *client)
{
  end_refusal (relay);
  rc_flow_init (&relay->bye);
  size_t size;
  unsigned char *space = rc_flow_space (&relay->bye, &size);
  rc_flow_fill (&relay->bye,
		rd_encode_end_session (RD_END_NO_SESSION, space, size));
  relay->refused = client;
  relay->bye_deadline = rc_now_ms () + END_LIMIT;
  relay->stopping = true;
}

/* Fill the entry of the refused client of RELAY in FDS.  While BYE has
   bytes, wait until the client can take them.  Then wait for its
   close.  */
static void
watch_refused (const struct relay *relay, struct pollfd *fds)
{
  struct rc_stream *refused = relay->refused;
  short events = rc_flow_empty_p (&relay->bye) ? POLLIN : POLLOUT;
  fds[WATCH_REFUSED]
    = (refused
       ? (struct pollfd) { rc_stream_fd (refused),
			   rc_stream_events (refused, events), 0 }
       : (struct pollfd) { -1, 0, 0 });
}

/* Return the wait WAIT of poll in milliseconds, or the time to the
   deadline of the refused client of RELAY if that is shorter.  -1 is
   no limit.  */
static int64_t
refusal_wait (const struct relay *relay, int64_t wait)
{
  if (!relay->refused)
    return wait;
  int64_t left = relay->bye_deadline - rc_now_ms ();
  left = left < 0 ? 0 : left;
  return wait < 0 || left < wait ? left : wait;
}

/* Send the bytes of BYE to the refused client of RELAY after the events
   of FDS, or read the bytes of the client until its close.  Close the
   client at its close, at an error, or at its deadline.  */
static void
move_refused (struct relay *relay, const struct pollfd *fds)
{
  struct rc_stream *refused = relay->refused;
  if (!refused)
    return;
  bool done = rc_now_ms () >= relay->bye_deadline;
  if (!done && fds[WATCH_REFUSED].revents)
    done = (rc_flow_empty_p (&relay->bye) ? ended_p (refused)
	    : !drain_flow (&relay->bye, refused));
  if (done)
    end_refusal (relay);
}

/* Read and drop the bytes of STREAM that are ready.  Return true if
   STREAM ends or fails.  */
static bool
ended_p (struct rc_stream *stream)
{
  for (;;)
    {
      unsigned char bytes[RC_STREAM_RECORD_SIZE];
      ptrdiff_t count = rc_stream_read (stream, bytes, sizeof bytes);
      if (count == 0 || count == RC_STREAM_FAIL)
	return true;
      if (count == RC_STREAM_WAIT)
	return false;
    }
}

/* Close the refused client of RELAY, if any.  */
static void
end_refusal (struct relay *relay)
{
  rc_stream_close (relay->refused);
  relay->refused = NULL;
}

/* Move bytes of FLOW from the stream FROM to the stream TO after the
   events FROM_EVENTS and TO_EVENTS of their sockets.  Return false
   when a stream ends or fails.  */
static bool
move (struct rc_flow *flow, struct rc_stream *from, short from_events,
      struct rc_stream *to, short to_events)
{
  if (rc_flow_empty_p (flow))
    return (!(from_events & (POLLIN | POLLHUP | POLLERR))
	    || fill_flow (flow, from));
  return (!(to_events & (POLLOUT | POLLHUP | POLLERR))
	  || drain_flow (flow, to));
}

/* Read the next bytes of the stream FROM into FLOW.  Return false
   when FROM ends or fails.  */
static bool
fill_flow (struct rc_flow *flow, struct rc_stream *from)
{
  size_t size;
  unsigned char *space = rc_flow_space (flow, &size);
  ptrdiff_t count = rc_stream_read (from, space, size);
  if (count > 0)
    rc_flow_fill (flow, count);
  return count > 0 || count == RC_STREAM_WAIT;
}

/* Write the bytes of FLOW to the stream TO, as many as it takes now.
   Return false when TO fails.  When TO closed its connection, drop the
   bytes, and return true: the bytes that TO sent before its close go
   on in the other direction, and the read of TO gives the end.  */
static bool
drain_flow (struct rc_flow *flow, struct rc_stream *to)
{
  size_t size;
  const unsigned char *bytes = rc_flow_bytes (flow, &size);
  ptrdiff_t count = rc_stream_write (to, bytes, size);
  if (count == RC_STREAM_CLOSED)
    count = size;
  if (count > 0)
    rc_flow_drain (flow, count);
  return count >= 0 || count == RC_STREAM_WAIT;
}

/* Return true if PATH is a socket.  */
static bool
session_socket_p (const char *path)
{
  struct stat status;
  return stat (path, &status) == 0 && S_ISSOCK (status.st_mode);
}

/* Listen on the first free port of the range of SPEC, at its address.
   Put the port in *PORT, and return the listening socket, or -1 on an
   error.  */
static int
listen_in_range (const struct rc_listen_spec *spec, uint16_t *port)
{
  for (uint32_t each = spec->low; each <= spec->high; each++)
    {
      int fd = listen_on (spec, each);
      if (fd >= 0)
	{
	  *port = each;
	  return fd;
	}
      if (errno != EADDRINUSE)
	{
	  perror ("remote-client: listen");
	  return -1;
	}
    }
  char text[RC_LISTEN_TEXT_SIZE];
  rc_listen_spec_format (spec, text);
  fprintf (stderr, "remote-client: %s: no port is free\n", text);
  return -1;
}

/* Listen on PORT at the address of SPEC.  Return the listening socket,
   or -1 with errno set.  */
static int
listen_on (const struct rc_listen_spec *spec, uint16_t port)
{
  struct sockaddr_storage address;
  socklen_t size = rc_listen_spec_address (spec, port, &address);
  int fd = socket (address.ss_family,
		   SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd >= 0 && bind (fd, (struct sockaddr *) &address, size) == 0
      && listen (fd, BACKLOG) == 0)
    return fd;
  int error = errno;
  if (fd >= 0)
    close (fd);
  errno = error;
  return -1;
}
