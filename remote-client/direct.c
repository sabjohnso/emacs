/* The direct connection of remote-client to a relay.

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

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "direct.h"
#include "tls.h"

/* A direct connector: the host and the line of the relay.  */
struct rc_direct
{
  char *host;
  struct rc_relay_line line;
};

static int connect_any (const struct rc_direct *);
static int connect_address (const struct addrinfo *);
static bool wait_connected (int);
static struct rc_stream *start_session (const struct rc_direct *, int);

struct rc_direct *
rc_direct_new (const char *host, const struct rc_relay_line *line)
{
  struct rc_direct *direct = malloc (sizeof *direct);
  char *copy = strdup (host);
  if (!direct || !copy)
    {
      free (direct);
      free (copy);
      return NULL;
    }
  direct->host = copy;
  direct->line = *line;
  return direct;
}

struct rc_stream *
rc_direct_connect (void *data)
{
  const struct rc_direct *direct = data;
  int fd = connect_any (direct);
  return fd < 0 ? NULL : start_session (direct, fd);
}

void
rc_direct_free (struct rc_direct *direct)
{
  if (direct)
    free (direct->host);
  free (direct);
}

/* Resolve the host of DIRECT, and return a connection to the port of
   the relay at the first address that answers, or -1.  The socket
   blocks.  */
static int
connect_any (const struct rc_direct *direct)
{
  char port[sizeof "65535"];
  snprintf (port, sizeof port, "%u", (unsigned) direct->line.port);
  struct addrinfo hints = { .ai_family = AF_UNSPEC,
			    .ai_socktype = SOCK_STREAM,
			    .ai_flags = AI_NUMERICSERV };
  struct addrinfo *list;
  if (getaddrinfo (direct->host, port, &hints, &list) != 0)
    return -1;
  int fd = -1;
  for (const struct addrinfo *each = list; each && fd < 0;
       each = each->ai_next)
    fd = connect_address (each);
  freeaddrinfo (list);
  return fd;
}

/* Return a connection to ADDRESS, or -1 if it does not connect in
   RC_DIRECT_LIMIT milliseconds.  The socket blocks.  */
static int
connect_address (const struct addrinfo *address)
{
  int fd = socket (address->ai_family,
		   address->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
		   address->ai_protocol);
  if (fd < 0)
    return -1;
  bool connected
    = (connect (fd, address->ai_addr, address->ai_addrlen) == 0
       || (errno == EINPROGRESS && wait_connected (fd)));
  if (!connected || fcntl (fd, F_SETFL, 0) != 0)
    {
      close (fd);
      return -1;
    }
  return fd;
}

/* Return true if the connect of FD, which does not block, ends with
   success in RC_DIRECT_LIMIT milliseconds.  */
static bool
wait_connected (int fd)
{
  struct pollfd ready = { .fd = fd, .events = POLLOUT };
  int error;
  socklen_t size = sizeof error;
  return poll (&ready, 1, RC_DIRECT_LIMIT) == 1
    && getsockopt (fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0
    && error == 0;
}

/* Make a TLS session on the connection FD with the key of the pin of
   DIRECT, and send the token first.  Return the stream, or NULL; then
   FD is closed.  The relay closes a connection with no token after
   RC_RELAY_TOKEN_LIMIT, so the handshake waits no longer.  */
static struct rc_stream *
start_session (const struct rc_direct *direct, int fd)
{
  struct rc_stream *stream
    = rc_tls_connect (fd, direct->line.pin, RC_RELAY_TOKEN_LIMIT);
  if (stream
      && rc_stream_write (stream, direct->line.token, RC_TOKEN_SIZE)
	 != RC_TOKEN_SIZE)
    {
      rc_stream_close (stream);
      return NULL;
    }
  return stream;
}
