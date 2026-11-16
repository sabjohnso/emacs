/* The paint of a root frame and its children for the remote display
   client.

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

/* The window of a root frame shows the surface of the root and the
   surfaces of its visible children.  rc_compose paints the layers of
   the root (see stack.h) in the paint order.  Each layer is clipped to
   its clip, so a child does not go outside its ancestors.  Thus the
   paint agrees with rc_stack_frame_at: the frame that the window
   shows at a point is the frame that gets the input at that point.

   The module uses only Cairo, so it needs no display.  */

#ifndef REMOTE_CLIENT_COMPOSE_H
#define REMOTE_CLIENT_COMPOSE_H

#include <stdint.h>

#include <cairo.h>

#include "stack.h"

/* The function that gives the surface of a frame.  Return the surface
   of FRAME, or NULL if FRAME has no surface.  DATA is the data that
   the caller gave to rc_compose.  */
typedef cairo_surface_t *(*rc_compose_surface_function) (void *data,
							 uint32_t frame);

/* Paint the layers of ROOT in STACK to CR, in the coordinates of
   ROOT.  SURFACE gives the surface of each frame, with DATA.  A layer
   with no surface is not painted.  Give the surfaces from the source
   of the sizes of STACK, so that each layer has a surface.  rc_compose
   does not change the parts of CR that no layer covers.  */
extern void rc_compose (cairo_t *cr, const struct rc_stack *stack,
			uint32_t root, rc_compose_surface_function surface,
			void *data);

#endif /* REMOTE_CLIENT_COMPOSE_H */
