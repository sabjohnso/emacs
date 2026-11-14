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
#include <time.h>
#include <unistd.h>

#include "flow.h"
#include "pending.h"
#include "relay.h"
#include "relayline.h"
#include "token.h"
#include "unixsocket.h"

/* The state of a relay.  CLIENT and SERVER are the connected client
   and its connection to the session, or -1.  UP holds the bytes of
   the client for the session, and DOWN the bytes of the session for
   the client.  */
struct relay
{
  const char *session;
  uint8_t token[RC_TOKEN_SIZE];
  int listener;
  struct rc_pending_table pending;
  int client, server;
  struct rc_flow up, down;
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

/* The positions of the descriptors in the array of poll.  */
enum
  {
    WATCH_LISTENER,
    WATCH_CLIENT,
    WATCH_SERVER,
    WATCH_PENDING,
    WATCH_SIZE = WATCH_PENDING + RC_PENDING_SIZE,
  };

static bool prepare (struct relay *, const char *, struct rc_relay_line *);
static enum daemon start_daemon (void);
static void announce (const struct rc_relay_line *);
static void serve (struct relay *);
static int watch (const struct relay *, struct pollfd *);
static void watch_pair (const struct relay *, struct pollfd *);
static void move_pair (struct relay *, const struct pollfd *);
static bool read_tokens (struct relay *, const struct pollfd *);
static void accept_client (struct relay *);
static void expire_pending (struct relay *);
static enum rc_pending_result read_token (struct relay *, int);
static bool open_pair (struct relay *, int);
static void close_pair (struct relay *);
static bool move (struct rc_flow *, int, short, int, short);
static bool fill_flow (struct rc_flow *, int);
static bool drain_flow (struct rc_flow *, int);
static bool session_socket_p (const char *);
static int listen_on_loopback (uint16_t *);
static int64_t now_ms (void);

int
rc_relay_run (const char *session)
{
  struct relay relay;
  struct rc_relay_line line;
  if (!prepare (&relay, session, &line))
    return 1;
  switch (start_daemon ())
    {
    case DAEMON_PARENT:
      return 0;
    case DAEMON_FAILED:
      return 1;
    case DAEMON_CHILD:
      announce (&line);
      serve (&relay);
      return 0;
    }
  return 1;
}

/* Check the session socket SESSION, make the token, and listen.  Put
   the state in RELAY and the line in LINE.  Return false on an
   error.  */
static bool
prepare (struct relay *relay, const char *session,
	 struct rc_relay_line *line)
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
  relay->listener = listen_on_loopback (&line->port);
  if (relay->listener < 0)
    return false;
  relay->session = session;
  memcpy (relay->token, line->token, sizeof relay->token);
  rc_pending_init (&relay->pending);
  relay->client = relay->server = -1;
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

/* Serve the clients of RELAY until the session stops.  */
static void
serve (struct relay *relay)
{
  for (;;)
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
      move_pair (relay, fds);
      if (!read_tokens (relay, fds))
	return;
      if (fds[WATCH_LISTENER].revents)
	accept_client (relay);
      expire_pending (relay);
    }
}

/* Fill FDS with the descriptors of RELAY and their events.  Return the
   wait of poll in milliseconds: the time to the first deadline of a
   pending connection, or -1 for no limit.  */
static int
watch (const struct relay *relay, struct pollfd *fds)
{
  fds[WATCH_LISTENER] = (struct pollfd) { relay->listener, POLLIN, 0 };
  watch_pair (relay, fds);
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    fds[WATCH_PENDING + i]
      = (struct pollfd) { rc_pending_fd (&relay->pending, i), POLLIN, 0 };
  int64_t wait = rc_pending_wait (&relay->pending, now_ms ());
  assert (wait <= RC_RELAY_TOKEN_LIMIT);
  return wait;
}

/* Fill the entries of the pair of RELAY in FDS.  A direction with
   bytes waits until its destination can take them.  A direction with
   no bytes waits for its source.  A descriptor with no event is -1,
   so poll does not report its end again and again.  */
static void
watch_pair (const struct relay *relay, struct pollfd *fds)
{
  short client = 0, server = 0;
  if (relay->client >= 0)
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
  fds[WATCH_CLIENT] = (struct pollfd) { client ? relay->client : -1,
					client, 0 };
  fds[WATCH_SERVER] = (struct pollfd) { server ? relay->server : -1,
					server, 0 };
}

/* Move the bytes of the pair of RELAY after the events of FDS.  Close
   the pair when the client or the session closes its connection.  A
   direction reads only when it has no bytes, so the bytes that are
   left go to a peer that has closed.  */
static void
move_pair (struct relay *relay, const struct pollfd *fds)
{
  if (relay->client < 0)
    return;
  short client = fds[WATCH_CLIENT].revents;
  short server = fds[WATCH_SERVER].revents;
  if (!move (&relay->up, relay->client, client, relay->server, server)
      || !move (&relay->down, relay->server, server, relay->client, client))
    close_pair (relay);
}

/* Read the tokens of the pending connections of RELAY after the events
   of FDS.  Remove each connection with a complete token or an end from
   the table.  Return false when the relay must stop, because it cannot
   connect to the session.  */
static bool
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
      rc_pending_remove (rc_pending_find (&relay->pending, fd));
      if (result == RC_PENDING_BAD)
	close (fd);
      else if (!open_pair (relay, fd))
	return false;
    }
  return true;
}

/* Accept a new connection of RELAY, and make it wait for its token.
   Close the connection that the table removes, if any.  */
static void
accept_client (struct relay *relay)
{
  int fd = accept (relay->listener, NULL, NULL);
  if (fd < 0)
    return;
  fcntl (fd, F_SETFL, O_NONBLOCK);
  int removed = rc_pending_add (&relay->pending, fd,
				now_ms () + RC_RELAY_TOKEN_LIMIT);
  if (removed >= 0)
    close (removed);
}

/* Close each pending connection of RELAY whose deadline has passed.  */
static void
expire_pending (struct relay *relay)
{
  int64_t now = now_ms ();
  for (int fd; (fd = rc_pending_expired (&relay->pending, now)) >= 0;)
    close (fd);
}

/* Read the next bytes of the token of the pending connection FD of
   RELAY, and return the result.  The end of the connection is a wrong
   token.  */
static enum rc_pending_result
read_token (struct relay *relay, int fd)
{
  struct rc_pending *slot = rc_pending_find (&relay->pending, fd);
  assert (slot);
  size_t size;
  uint8_t *space = rc_pending_space (slot, &size);
  ssize_t count = read (fd, space, size);
  if (count < 0 && (errno == EAGAIN || errno == EINTR))
    return RC_PENDING_MORE;
  return (count > 0 ? rc_pending_fill (slot, count, relay->token)
	  : RC_PENDING_BAD);
}

/* Make FD the client of RELAY, and connect it to the session.  Close
   the old pair.  If the relay cannot connect to the session, close FD
   and return false.  */
static bool
open_pair (struct relay *relay, int fd)
{
  close_pair (relay);
  int server = rc_connect_unix (relay->session, false);
  if (server < 0)
    {
      close (fd);
      return false;
    }
  fcntl (server, F_SETFL, O_NONBLOCK);
  relay->client = fd;
  relay->server = server;
  rc_flow_init (&relay->up);
  rc_flow_init (&relay->down);
  return true;
}

/* Close the client of RELAY and its connection to the session.  */
static void
close_pair (struct relay *relay)
{
  if (relay->client < 0)
    return;
  close (relay->client);
  close (relay->server);
  relay->client = relay->server = -1;
}

/* Move bytes of FLOW from the descriptor FROM to the descriptor TO
   after their events FROM_EVENTS and TO_EVENTS.  Return false when a
   connection ends or fails.  */
static bool
move (struct rc_flow *flow, int from, short from_events, int to,
      short to_events)
{
  if (rc_flow_empty_p (flow))
    return (!(from_events & (POLLIN | POLLHUP | POLLERR))
	    || fill_flow (flow, from));
  return (!(to_events & (POLLOUT | POLLHUP | POLLERR))
	  || drain_flow (flow, to));
}

/* Read the next bytes of the descriptor FROM into FLOW.  Return false
   when FROM ends or fails.  */
static bool
fill_flow (struct rc_flow *flow, int from)
{
  size_t size;
  unsigned char *space = rc_flow_space (flow, &size);
  ssize_t count = read (from, space, size);
  if (count > 0)
    rc_flow_fill (flow, count);
  if (count < 0)
    return errno == EAGAIN || errno == EINTR;
  return count > 0;
}

/* Write the bytes of FLOW to the descriptor TO, as many as it takes
   now.  Return false when TO fails.  */
static bool
drain_flow (struct rc_flow *flow, int to)
{
  size_t size;
  const unsigned char *bytes = rc_flow_bytes (flow, &size);
  ssize_t count = send (to, bytes, size, MSG_NOSIGNAL);
  if (count > 0)
    rc_flow_drain (flow, count);
  if (count < 0)
    return errno == EAGAIN || errno == EINTR;
  return true;
}

/* Return true if PATH is a socket.  */
static bool
session_socket_p (const char *path)
{
  struct stat status;
  return stat (path, &status) == 0 && S_ISSOCK (status.st_mode);
}

/* Listen on a new port of the loopback address.  Put the port in
   *PORT, and return the listening socket, or -1 on an error.  */
static int
listen_on_loopback (uint16_t *port)
{
  struct sockaddr_in address = { .sin_family = AF_INET,
				 .sin_addr = { htonl (INADDR_LOOPBACK) } };
  socklen_t size = sizeof address;
  int fd = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0
      || bind (fd, (struct sockaddr *) &address, sizeof address) != 0
      || listen (fd, BACKLOG) != 0
      || getsockname (fd, (struct sockaddr *) &address, &size) != 0)
    {
      perror ("remote-client: listen");
      if (fd >= 0)
	close (fd);
      return -1;
    }
  *port = ntohs (address.sin_port);
  return fd;
}

/* Return the time of a monotonic clock in milliseconds.  */
static int64_t
now_ms (void)
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
