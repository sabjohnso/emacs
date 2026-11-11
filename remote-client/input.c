/* The input messages of the remote display client.

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

#include "input.h"

#include <math.h>

static uint8_t modifier_bits (GdkModifierType);

struct rd_message
rc_key_message (uint32_t frame, guint keyval, GdkModifierType state,
		guint32 time)
{
  return (struct rd_message)
    { .type = RD_KEY,
      .u.key = { .frame = frame, .time = time, .keysym = keyval,
		 .character = gdk_keyval_to_unicode (keyval),
		 .modifiers = modifier_bits (state) } };
}

struct rd_message
rc_button_message (uint32_t frame, guint button, double x, double y,
		   GdkModifierType state, guint32 time, bool press)
{
  return (struct rd_message)
    { .type = RD_BUTTON,
      .u.button = { .frame = frame, .time = time, .x = floor (x),
		    .y = floor (y), .button = button,
		    .modifiers = modifier_bits (state), .press = press } };
}

struct rd_message
rc_motion_message (uint32_t frame, double x, double y, guint32 time)
{
  return (struct rd_message)
    { .type = RD_MOTION,
      .u.motion = { .frame = frame, .time = time, .x = floor (x),
		    .y = floor (y) } };
}

int
rc_scroll_button (GdkScrollDirection direction, double dx, double dy)
{
  switch (direction)
    {
    case GDK_SCROLL_UP:
      return RD_BUTTON_WHEEL_UP;
    case GDK_SCROLL_DOWN:
      return RD_BUTTON_WHEEL_DOWN;
    case GDK_SCROLL_LEFT:
      return RD_BUTTON_WHEEL_LEFT;
    case GDK_SCROLL_RIGHT:
      return RD_BUTTON_WHEEL_RIGHT;
    default:
      if (fabs (dy) >= fabs (dx) && dy != 0)
	return dy < 0 ? RD_BUTTON_WHEEL_UP : RD_BUTTON_WHEEL_DOWN;
      if (dx != 0)
	return dx < 0 ? RD_BUTTON_WHEEL_LEFT : RD_BUTTON_WHEEL_RIGHT;
      return 0;
    }
}

struct rd_message
rc_focus_message (uint32_t frame, bool in)
{
  return (struct rd_message)
    { .type = RD_FOCUS, .u.focus = { .frame = frame, .in = in } };
}

struct rd_message
rc_resize_message (uint32_t frame, int width, int height)
{
  return (struct rd_message)
    { .type = RD_RESIZE,
      .u.resize = { .frame = frame, .width = MAX (width, 0),
		    .height = MAX (height, 0) } };
}

/* Return the modifier bits of the GDK state STATE.  Mod1, the Alt key
   of most keyboards, is meta, as in pgtkterm.c with its default
   settings.  */
static uint8_t
modifier_bits (GdkModifierType state)
{
  return (((state & GDK_SHIFT_MASK) ? RD_SHIFT : 0)
	  | ((state & GDK_CONTROL_MASK) ? RD_CONTROL : 0)
	  | ((state & (GDK_MOD1_MASK | GDK_META_MASK)) ? RD_META : 0)
	  | ((state & GDK_SUPER_MASK) ? RD_SUPER : 0)
	  | ((state & GDK_HYPER_MASK) ? RD_HYPER : 0));
}
