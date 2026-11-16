/* The places of the frames of the remote display client.

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

/* A stack keeps the place of each frame that a place-frame message
   gave: its parent, its position in the parent, its Z value and its
   visibility.  A frame with no place is a visible root: a frame with a
   window of its own.  A child frame is painted in the window of its
   root.

   The stack keeps only the places.  It gets the size of a frame from
   a function that the caller gives.  A frame with no size is not
   painted, and its children are not painted.

   The layers of a root are the frames that the window of the root
   shows, in the paint order: a frame comes before its children, and
   the frames with the same parent come in the order of Z (and of the
   frame number if two Z values are equal).  All the children of a
   frame come immediately after it.  A frame is in the layers only if
   it, its parent and each other ancestor are visible and have a size.

   The stack needs no display, no GTK and no Cairo.  */

#ifndef REMOTE_CLIENT_STACK_H
#define REMOTE_CLIENT_STACK_H

#include <stdbool.h>
#include <stdint.h>

#include "rdproto.h"

/* The stack of the frames of one connection.  Only the functions of
   stack.c use its fields.  */
struct rc_stack;

/* The function that gives the size of a frame.  Put the size of FRAME
   in WIDTH and HEIGHT and return true, or return false if FRAME has no
   size.  DATA is the data that the caller gave to rc_stack_new.  */
typedef bool (*rc_stack_size_function) (void *data, uint32_t frame,
					int *width, int *height);

/* A rectangle in pixels.  */
struct rc_rect
{
  int x;
  int y;
  int width;
  int height;
};

/* One frame that the window of a root shows.  X and Y are the position
   of the top-left corner of FRAME in the root.  CLIP is the part of the
   root where FRAME is visible: the area of FRAME, clipped to the area
   of each ancestor.  CLIP can be empty.  */
struct rc_layer
{
  uint32_t frame;
  int64_t x;
  int64_t y;
  struct rc_rect clip;
};

/* The frame under a point, and the point in the coordinates of that
   frame.  */
struct rc_hit
{
  uint32_t frame;
  int x;
  int y;
};

/* Return a new stack with no places.  SIZE gives the sizes of the
   frames, with DATA.  */
extern struct rc_stack *rc_stack_new (rc_stack_size_function size,
				      void *data);

/* Free the memory of STACK.  */
extern void rc_stack_free (struct rc_stack *stack);

/* Keep the place PLACE of the frame PLACE->frame in STACK.  It replaces
   the last place of that frame.  */
extern void rc_stack_place (struct rc_stack *stack,
			    const struct rd_place_frame *place);

/* Remove the place of FRAME from STACK.  FRAME is then a visible root
   again.  Use this when the server destroys FRAME.  */
extern void rc_stack_forget (struct rc_stack *stack, uint32_t frame);

/* Remove all places from STACK.  Use this for a new connection.  */
extern void rc_stack_reset (struct rc_stack *stack);

/* Return the parent of FRAME in STACK, or 0 if FRAME is a root.  */
extern uint32_t rc_stack_parent (const struct rc_stack *stack,
				 uint32_t frame);

/* Return the last frame of the chain of parents of FRAME in STACK:
   FRAME itself if FRAME is a root.  Return 0 if the chain has a
   loop.  */
extern uint32_t rc_stack_root (const struct rc_stack *stack,
			       uint32_t frame);

/* Return false if the last place of FRAME in STACK hides FRAME, and
   true if it shows FRAME or if FRAME has no place.  */
extern bool rc_stack_visible_p (const struct rc_stack *stack,
				uint32_t frame);

/* Call FUNCTION with each layer of ROOT in the paint order, and with
   DATA.  A frame that is not a root has no layers.  */
extern void rc_stack_each_layer (const struct rc_stack *stack,
				 uint32_t root,
				 void (*function) (const struct rc_layer *layer,
						   void *data),
				 void *data);

/* Find the frame that the window of ROOT shows at the point X, Y of
   ROOT: the last layer in the paint order whose clip contains the
   point.  Put the frame and the point in its coordinates in HIT, and
   return true.  Return false if no layer contains the point.  */
extern bool rc_stack_frame_at (const struct rc_stack *stack, uint32_t root,
			       int x, int y, struct rc_hit *hit);

/* Put the position of the top-left corner of FRAME in ROOT in X and
   Y, and return true.  Return false if FRAME is not a layer of ROOT.
   The position can be outside ROOT.  */
extern bool rc_stack_origin (const struct rc_stack *stack, uint32_t root,
			     uint32_t frame, int64_t *x, int64_t *y);

#endif /* REMOTE_CLIENT_STACK_H */
