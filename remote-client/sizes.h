/* The sizes of a window of the remote display client.

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

/* The size of the window of a frame changes in two ways.  The user
   changes it, and the client sends a resize message.  Or the server
   defines the frame with a size, and the client gives that size to
   the window.

   The server answers each resize message that changes the frame with
   a define-frame message.  An answer can come while the user still
   changes the size, so it can give an older size.  The window must
   not take the size of an answer.  It must take the size of each
   other define-frame message.

   A struct rc_sizes decides this for one window.  It keeps the sizes
   that the client sent and has no answer for, and the size that the
   server knows.  It needs no display.  */

#ifndef REMOTE_CLIENT_SIZES_H
#define REMOTE_CLIENT_SIZES_H

#include <stdbool.h>
#include <stdint.h>

/* A size in pixels.  */
struct rc_size
{
  uint32_t width;
  uint32_t height;
};

/* The most sizes with no answer that a struct rc_sizes keeps.  When it
   has more, it forgets the oldest.  */
enum { RC_SIZES_LIMIT = 16 };

/* The sizes of one window.  Only the functions of sizes.c use the
   fields.  */
struct rc_sizes
{
  struct rc_size known;
  struct rc_size sent[RC_SIZES_LIMIT];
  int nsent;
};

/* Return the sizes of a new window of a frame that the server defined
   with the size SIZE.  */
extern struct rc_sizes rc_sizes_new (struct rc_size size);

/* Return the size of a window allocation of WIDTH, HEIGHT.  A negative
   width or height becomes 0.  */
extern struct rc_size rc_size_of (int width, int height);

/* Record in SIZES that the window got the size SIZE.  Return true if
   the client must send a resize message for SIZE: the server does not
   know SIZE.  */
extern bool rc_sizes_note_allocation (struct rc_sizes *sizes,
				      struct rc_size size);

/* Record in SIZES that the server defined the frame of the window with
   the size SIZE.  Return true if the window must take SIZE: SIZE is
   not the answer to a resize message.  */
extern bool rc_sizes_note_definition (struct rc_sizes *sizes,
				      struct rc_size size);

#endif /* REMOTE_CLIENT_SIZES_H */
