/* The streams of remote-client.


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
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "stream.h"
#include "streamclass.h"

static ptrdiff_t read_socket (struct rc_stream *, void *, size_t);
static ptrdiff_t write_socket (struct rc_stream *, const void *, size_t);
static short socket_events (const struct rc_stream *, short);
static bool socket_buffered_p (const struct rc_stream *);
static void close_socket (struct rc_stream *);
static ptrdiff_t socket_result (ssize_t);

/* The kind of a socket stream.  */
static const struct rc_stream_class socket_class =
  { read_socket, write_socket, socket_events, socket_buffered_p,
    close_socket };

struct rc_stream *
rc_stream_socket (int fd)
{
  struct rc_stream *stream = malloc (sizeof *stream);
  if (!stream)
    {
      close (fd);
      return NULL;
    }
  *stream = (struct rc_stream) { &socket_class, fd };
  return stream;
}

ptrdiff_t
rc_stream_read (struct rc_stream *stream, void *bytes, size_t size)
{
  return stream->class->read (stream, bytes, size);
}

ptrdiff_t
rc_stream_write (struct rc_stream *stream, const void *bytes, size_t size)
{
  return stream->class->write (stream, bytes, size);
}

int
rc_stream_fd (const struct rc_stream *stream)
{
  return stream->fd;
}

short
rc_stream_events (const struct rc_stream *stream, short events)
{
  return stream->class->events (stream, events);
}

bool
rc_stream_buffered_p (const struct rc_stream *stream)
{
  return stream->class->buffered_p (stream);
}

void
rc_stream_close (struct rc_stream *stream)
{
  if (stream)
    stream->class->close (stream);
}

/* The read function of a socket stream.  A peer that closed while
   bytes that it did not read waited gives ECONNRESET after its last
   bytes.  That is the end of the stream, as a write gives
   RC_STREAM_CLOSED for it.  */
static ptrdiff_t
read_socket (struct rc_stream *stream, void *bytes, size_t size)
{
  ssize_t count = read (stream->fd, bytes, size);
  if (count < 0 && errno == ECONNRESET)
    return 0;
  return socket_result (count);
}

/* The write function of a socket stream.  A peer that has closed the
   socket gives RC_STREAM_CLOSED, and no signal.  */
static ptrdiff_t
write_socket (struct rc_stream *stream, const void *bytes, size_t size)
{
  ssize_t count = send (stream->fd, bytes, size, MSG_NOSIGNAL);
  if (count < 0 && (errno == EPIPE || errno == ECONNRESET))
    return RC_STREAM_CLOSED;
  return socket_result (count);
}

/* The events function of a socket stream: the socket waits for the
   events of its user.  */
static short
socket_events (const struct rc_stream *stream, short events)
{
  (void) stream;
  return events;
}

/* The buffered function of a socket stream: the kernel holds all the
   bytes, so poll shows each of them.  */
static bool
socket_buffered_p (const struct rc_stream *stream)
{
  (void) stream;
  return false;
}

/* The close function of a socket stream.  */
static void
close_socket (struct rc_stream *stream)
{
  close (stream->fd);
  free (stream);
}

/* Return the result of a stream for the result COUNT of read or send
   on a socket.  */
static ptrdiff_t
socket_result (ssize_t count)
{
  if (count >= 0)
    return count;
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
    return RC_STREAM_WAIT;
  return RC_STREAM_FAIL;
}
