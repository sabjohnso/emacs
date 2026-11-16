/* The places of the frames of the remote display backend.

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

/* The client shows a child frame in the window of its root frame, at
   the place that a place-frame message gives (rdproto.h).  This file
   keeps the place of each frame on the client equal to the place of
   the frame in Emacs.  The terminal hooks below change a place, and
   send it at once, because redisplay does not flush a frame whose
   windows did not change.

   A frame with no place-frame message is a visible frame with a
   window of its own.  The session sends a place only when it is
   different from the place that the client has, so a session with
   only visible root frames sends no place-frame.  */

#include <config.h>

#include "lisp.h"
#include "frame.h"
#include "dispextern.h"
#include "rdproto.h"
#include "remoteterm.h"

static void remote_restack (struct frame *, bool);
static void remote_place_children (struct frame *);
static bool remote_send_place (struct frame *);
static struct remote_placement remote_placement_of (struct frame *);
static bool remote_placement_equal (struct remote_placement,
				    struct remote_placement);

/* The place that a client has for a frame before it gets a place-frame
   message for the frame.  */
static const struct remote_placement remote_default_placement =
  { .parent = 0, .x = 0, .y = 0, .z = 0, .visible = true };

/* The set_frame_offset_hook.  Move F to XOFF, YOFF if CHANGE_GRAVITY
   is positive.  Otherwise the caller set the position of F already.  A
   negative value counts from the right or the bottom edge.  */
void
remote_set_offset (struct frame *f, int xoff, int yoff, int change_gravity)
{
  if (change_gravity > 0)
    {
      f->left_pos = xoff;
      f->top_pos = yoff;
      f->size_hint_flags &= ~(XNegative | YNegative);
      if (xoff < 0)
	f->size_hint_flags |= XNegative;
      if (yoff < 0)
	f->size_hint_flags |= YNegative;
      f->win_gravity = NorthWestGravity;
    }
  remote_calc_absolute_position (f);
  remote_place_frame (f);
}

/* The frame_raise_lower_hook.  Put the child frame F above each of
   its siblings if RAISE, else below each of them.  A root frame does
   not change.  */
void
remote_raise_lower_frame (struct frame *f, bool raise)
{
  struct frame *parent = FRAME_PARENT_FRAME (f);
  if (!parent)
    return;
  remote_restack (f, raise);
  remote_place_children (parent);
}

/* The frame_visible_invisible_hook.  */
void
remote_frame_visible_invisible (struct frame *f, bool visible)
{
  SET_FRAME_VISIBLE (f, visible);
  remote_place_frame (f);
}

/* The handler of the parent-frame parameter.  The generic code
   accepts only a live frame of the same terminal, which is the same
   session, and refuses a cycle.  Put F above the other children of
   the new parent.  */
void
remote_set_parent_frame (struct frame *f, Lisp_Object new_value,
			 Lisp_Object old_value)
{
  struct frame *parent = NILP (new_value) ? NULL : XFRAME (new_value);
  if (parent == FRAME_PARENT_FRAME (f))
    return;
  if (parent)
    f->z_order = 1 + max_child_z_order (parent);
  fset_parent_frame (f, new_value);
  remote_calc_absolute_position (f);
  remote_place_frame (f);
}

/* Make a negative position of the child frame F count from the right
   or the bottom edge of its parent, as x_calc_absolute_position does.
   After this, left_pos and top_pos give the top-left corner of F in
   the parent.  A root frame keeps its position, because the client
   does not place a root frame.  */
void
remote_calc_absolute_position (struct frame *f)
{
  struct frame *parent = FRAME_PARENT_FRAME (f);
  if (!parent)
    return;
  if (f->size_hint_flags & XNegative)
    f->left_pos += FRAME_PIXEL_WIDTH (parent) - FRAME_PIXEL_WIDTH (f);
  if (f->size_hint_flags & YNegative)
    f->top_pos += FRAME_PIXEL_HEIGHT (parent) - FRAME_PIXEL_HEIGHT (f);
  f->size_hint_flags &= ~(XNegative | YNegative);
}

/* Send the place of F to its client, and flush it, if the client
   knows F and has a different place for it.  */
void
remote_place_frame (struct frame *f)
{
  if (FRAME_REMOTE_OUTPUT (f)->defined && remote_send_place (f))
    remote_flush (FRAME_DISPLAY_INFO (f));
}

/* Send the place of F before the first define-frame of F, if the
   place is not the default place.  The caller sends the define-frame
   and flushes.  */
void
remote_place_new_frame (struct frame *f)
{
  remote_send_place (f);
}

/* Make the client of F have no place for F, as before the first
   define-frame of F.  */
void
remote_forget_place (struct frame *f)
{
  FRAME_REMOTE_OUTPUT (f)->placed = remote_default_placement;
}

/* Give the siblings of the child frame F the z values 1 to N, in their
   current order, and give F the value N + 1 if RAISE, else 0.  Thus no
   two siblings have the same value.  */
static void
remote_restack (struct frame *f, bool raise)
{
  struct frame *parent = FRAME_PARENT_FRAME (f);
  Lisp_Object siblings = Qnil, tail, frame;
  FOR_EACH_FRAME (tail, frame)
    if (XFRAME (frame) != f && FRAME_PARENT_FRAME (XFRAME (frame)) == parent)
      siblings = Fcons (frame, siblings);
  siblings = CALLN (Fsort, siblings, QClessp, Qframe__z_order_lessp);

  int z = 0;
  FOR_EACH_TAIL (siblings)
    XFRAME (XCAR (siblings))->z_order = ++z;
  f->z_order = raise ? z + 1 : 0;
}

/* Send the place of each child of PARENT that changed, and flush once.  */
static void
remote_place_children (struct frame *parent)
{
  bool sent = false;
  Lisp_Object tail, frame;
  FOR_EACH_FRAME (tail, frame)
    {
      struct frame *f = XFRAME (frame);
      if (FRAME_PARENT_FRAME (f) == parent && FRAME_REMOTE_OUTPUT (f)->defined)
	sent |= remote_send_place (f);
    }
  if (sent)
    remote_flush (FRAME_DISPLAY_INFO (parent));
}

/* Send a place-frame message for F if the place of F is not the place
   that the client has.  Return true if this sent a message.  */
static bool
remote_send_place (struct frame *f)
{
  struct remote_placement place = remote_placement_of (f);
  struct remote_output *output = FRAME_REMOTE_OUTPUT (f);
  if (remote_placement_equal (place, output->placed))
    return false;
  struct rd_message message =
    {
      .type = RD_PLACE_FRAME,
      .u.place_frame = { .frame = FRAME_NATIVE_WINDOW (f),
			 .parent = place.parent, .x = place.x, .y = place.y,
			 .z = place.z, .visible = place.visible },
    };
  remote_send_message (FRAME_DISPLAY_INFO (f), &message);
  output->placed = place;
  return true;
}

/* Return the place of F in Emacs.  This only reads F.  A root frame
   has the position 0, 0 and the z value 0, because the client does not
   use them.  */
static struct remote_placement
remote_placement_of (struct frame *f)
{
  struct remote_placement place = remote_default_placement;
  struct frame *parent = FRAME_PARENT_FRAME (f);
  place.visible = FRAME_VISIBLE_P (f);
  if (parent)
    {
      place.parent = FRAME_NATIVE_WINDOW (parent);
      place.x = f->left_pos;
      place.y = f->top_pos;
      place.z = f->z_order;
    }
  return place;
}

/* Return true if the places A and B are equal.  */
static bool
remote_placement_equal (struct remote_placement a, struct remote_placement b)
{
  return (a.parent == b.parent && a.x == b.x && a.y == b.y && a.z == b.z
	  && a.visible == b.visible);
}
