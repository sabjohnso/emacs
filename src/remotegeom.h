/* Rectangle helpers of the remote display.

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

/* The functions of this file calculate with the rectangles of a
   frame.  They have no side effects, so each file of the remote
   display that draws can use them.  */

#ifndef EMACS_REMOTEGEOM_H
#define EMACS_REMOTEGEOM_H

#include "lisp.h"
#include "dispextern.h"

/* Return the part of the rectangle A inside the rectangle B.  Its size
   is 0 if they have no common pixels, and each user of the result
   rejects that size.  */
static inline Emacs_Rectangle
remote_intersection (Emacs_Rectangle a, Emacs_Rectangle b)
{
  int left = max (a.x, b.x);
  int top = max (a.y, b.y);
  int right = min (a.x + (int) a.width, b.x + (int) b.width);
  int bottom = min (a.y + (int) a.height, b.y + (int) b.height);
  return (Emacs_Rectangle) { left, top, max (right - left, 0),
			     max (bottom - top, 0) };
}

#endif /* EMACS_REMOTEGEOM_H */
