/* Test the socket streams of remote-client.

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

/* The examples of the causes of a failed write of a socket stream:

   - Closed: a write after the peer closed the connection gives
     RC_STREAM_CLOSED.
   - Full: a write to a socket that does not block, when the peer
     reads nothing and the buffer is full, gives RC_STREAM_WAIT.
   - Other: a write to a file that is not a socket gives
     RC_STREAM_FAIL, and not RC_STREAM_CLOSED.
   - Reset: the peer sends bytes and closes while bytes that it did
     not read wait.  Linux then gives ECONNRESET to the next read
     after those bytes.  The reads give the bytes of the peer and then
     the end (0), not RC_STREAM_FAIL.  */

#include <config.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "stream.h"

static bool closed_example (void);
static bool full_example (void);
static bool other_example (void);
static bool reset_example (void);

/* An example and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} examples[] =
  {
    { "closed", closed_example },
    { "full", full_example },
    { "other", other_example },
    { "reset", reset_example },
  };

int
main (void)
{
  for (size_t i = 0; i < sizeof examples / sizeof *examples; i++)
    if (!examples[i].check ())
      {
	fprintf (stderr, "test-stream: the %s example failed\n",
		 examples[i].name);
	return 1;
      }
  printf ("test-stream: %zu examples passed\n",
	  sizeof examples / sizeof *examples);
  return 0;
}

/* The closed example.  */
static bool
closed_example (void)
{
  int fds[2];
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    return false;
  struct rc_stream *stream = rc_stream_socket (fds[0]);
  close (fds[1]);
  bool passed = stream && rc_stream_write (stream, "x", 1) == RC_STREAM_CLOSED;
  if (stream)
    rc_stream_close (stream);
  return passed;
}

/* The full example.  */
static bool
full_example (void)
{
  int fds[2];
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    return false;
  fcntl (fds[0], F_SETFL, fcntl (fds[0], F_GETFL) | O_NONBLOCK);
  struct rc_stream *stream = rc_stream_socket (fds[0]);
  static const char bytes[4096];
  ptrdiff_t result = 0;
  for (int i = 0; stream && i < 100000 && result >= 0; i++)
    result = rc_stream_write (stream, bytes, sizeof bytes);
  if (stream)
    rc_stream_close (stream);
  close (fds[1]);
  return result == RC_STREAM_WAIT;
}

/* The other example.  */
static bool
other_example (void)
{
  int fds[2];
  if (pipe (fds) != 0)
    return false;
  struct rc_stream *stream = rc_stream_socket (fds[1]);
  bool passed = stream && rc_stream_write (stream, "x", 1) == RC_STREAM_FAIL;
  if (stream)
    rc_stream_close (stream);
  close (fds[0]);
  return passed;
}

/* The reset example.  */
static bool
reset_example (void)
{
  int fds[2];
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    return false;
  struct rc_stream *stream = rc_stream_socket (fds[0]);
  bool passed = (stream && rc_stream_write (stream, "unread", 6) == 6
		 && write (fds[1], "last", 4) == 4);
  close (fds[1]);
  char bytes[16];
  ptrdiff_t first = passed ? rc_stream_read (stream, bytes, sizeof bytes) : -1;
  ptrdiff_t end = passed ? rc_stream_read (stream, bytes, sizeof bytes) : -1;
  if (passed && !(first == 4 && end == 0))
    fprintf (stderr, "test-stream: reset: the reads gave %td and %td\n",
	     first, end);
  if (stream)
    rc_stream_close (stream);
  return passed && first == 4 && end == 0;
}
