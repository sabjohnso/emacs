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

/* The server answers the resize messages in their order, but it can
   skip some.  So the answer to a define-frame message is the oldest
   sent size that is equal to it.  The client then forgets that size
   and each older one.  */

#include <config.h>

#include <string.h>

#include "sizes.h"

static void remember_sent (struct rc_sizes *, struct rc_size);
static int oldest_sent (const struct rc_sizes *, struct rc_size);
static void forget_sent (struct rc_sizes *, int);
static bool same_size_p (struct rc_size, struct rc_size);

struct rc_sizes
rc_sizes_new (struct rc_size size)
{
  return (struct rc_sizes) { .known = size, .nsent = 0 };
}

struct rc_size
rc_size_of (int width, int height)
{
  return (struct rc_size) { width < 0 ? 0 : width, height < 0 ? 0 : height };
}

bool
rc_sizes_note_allocation (struct rc_sizes *sizes, struct rc_size size)
{
  if (same_size_p (size, sizes->known))
    return false;
  remember_sent (sizes, size);
  sizes->known = size;
  return true;
}

bool
rc_sizes_note_definition (struct rc_sizes *sizes, struct rc_size size)
{
  int answer = oldest_sent (sizes, size);
  if (answer >= 0)
    {
      forget_sent (sizes, answer + 1);
      return false;
    }
  sizes->known = size;
  return true;
}

/* Remember SIZE as the newest sent size of SIZES.  Forget the oldest
   one if SIZES has RC_SIZES_LIMIT of them.  */
static void
remember_sent (struct rc_sizes *sizes, struct rc_size size)
{
  if (sizes->nsent == RC_SIZES_LIMIT)
    forget_sent (sizes, 1);
  sizes->sent[sizes->nsent++] = size;
}

/* Return the index of the oldest sent size of SIZES that is equal to
   SIZE, or -1 if SIZES has none.  */
static int
oldest_sent (const struct rc_sizes *sizes, struct rc_size size)
{
  for (int i = 0; i < sizes->nsent; i++)
    if (same_size_p (sizes->sent[i], size))
      return i;
  return -1;
}

/* Forget the COUNT oldest sent sizes of SIZES.  */
static void
forget_sent (struct rc_sizes *sizes, int count)
{
  memmove (sizes->sent, sizes->sent + count,
	   (sizes->nsent - count) * sizeof *sizes->sent);
  sizes->nsent -= count;
}

/* Return true if A and B are the same size.  */
static bool
same_size_p (struct rc_size a, struct rc_size b)
{
  return a.width == b.width && a.height == b.height;
}
