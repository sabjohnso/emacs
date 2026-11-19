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

/* A stream is a connection that reads and writes bytes: a socket, or
   a TLS session on a socket.  The relay and the window mode use a
   stream, so they do not know the kind of the connection.

   A stream does not change the mode of its socket.  On a socket that
   does not block, a read or a write that cannot go on now gives
   RC_STREAM_WAIT.  Then poll the socket of the stream, and try
   again.  */

#ifndef REMOTE_CLIENT_STREAM_H
#define REMOTE_CLIENT_STREAM_H

#include <stdbool.h>
#include <stddef.h>

struct rc_stream;

/* A read of this number of bytes takes all the bytes that a stream
   holds: a TLS record has at most this number of bytes.  */
enum { RC_STREAM_RECORD_SIZE = 16384 };

/* The results of a read or a write that are not a count of bytes.  */
enum
  {
    /* The stream cannot go on now.  */
    RC_STREAM_WAIT = -1,
    /* The stream has an error.  */
    RC_STREAM_FAIL = -2,
    /* A write failed because the peer closed the connection.  The
       bytes that the peer sent before its close can still come.  */
    RC_STREAM_CLOSED = -3,
  };

/* Return a new stream of the connected socket FD.  The stream owns
   FD.  If there is no memory, close FD and return NULL.  */
extern struct rc_stream *rc_stream_socket (int fd);

/* Read at most SIZE bytes of STREAM into BYTES.  Return the number of
   bytes, 0 at the end of the stream, RC_STREAM_WAIT, or
   RC_STREAM_FAIL.  */
extern ptrdiff_t rc_stream_read (struct rc_stream *stream, void *bytes,
				 size_t size);

/* Write at most SIZE BYTES to STREAM.  Return the number of bytes,
   RC_STREAM_WAIT, RC_STREAM_CLOSED, or RC_STREAM_FAIL.  After
   RC_STREAM_WAIT, write the same bytes again.  A socket stream gives
   RC_STREAM_CLOSED for a close of the peer; a TLS stream gives
   RC_STREAM_FAIL for it.  */
extern ptrdiff_t rc_stream_write (struct rc_stream *stream,
				  const void *bytes, size_t size);

/* Return the socket of STREAM, for poll.  */
extern int rc_stream_fd (const struct rc_stream *stream);

/* Return the events of poll that STREAM waits for, when its user
   waits for EVENTS.  In a handshake of TLS, a read can wait for
   POLLOUT, and a write for POLLIN.  */
extern short rc_stream_events (const struct rc_stream *stream,
			       short events);

/* Return true if STREAM holds bytes for a read that poll does not
   show on its socket.  Then read STREAM before the next poll.  After a
   read of at least RC_STREAM_RECORD_SIZE bytes, STREAM holds no such
   bytes.  */
extern bool rc_stream_buffered_p (const struct rc_stream *stream);

/* Close STREAM and its socket, and free STREAM.  STREAM can be
   NULL.  */
extern void rc_stream_close (struct rc_stream *stream);

#endif /* REMOTE_CLIENT_STREAM_H */
