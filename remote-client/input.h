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

/* This module makes the messages of the input of the user from the
   values of GDK events.  It needs no display, so a test can use it
   with no display.  */

#ifndef REMOTE_CLIENT_INPUT_H
#define REMOTE_CLIENT_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <gdk/gdk.h>

#include "rdproto.h"

/* Return the key message of a press of the key KEYVAL, with the GDK
   modifier state STATE, at the time TIME, in the frame FRAME.  */
extern struct rd_message rc_key_message (uint32_t frame, guint keyval,
					 GdkModifierType state,
					 guint32 time);

/* Return the button message of a press (PRESS is true) or a release
   of the mouse button BUTTON at X, Y, with the GDK modifier state
   STATE, at the time TIME, in the frame FRAME.  */
extern struct rd_message rc_button_message (uint32_t frame, guint button,
					    double x, double y,
					    GdkModifierType state,
					    guint32 time, bool press);

/* Return the motion message of a move of the mouse to X, Y at the time
   TIME in the frame FRAME.  */
extern struct rd_message rc_motion_message (uint32_t frame, double x,
					    double y, guint32 time);

/* Return the wheel button of X for a scroll in the direction
   DIRECTION, or for a smooth scroll with the deltas DX and DY.  Return
   0 for a smooth scroll with no delta.  */
extern int rc_scroll_button (GdkScrollDirection direction, double dx,
			     double dy);

/* Return the focus message of the frame FRAME, which got (IN is true)
   or lost the focus.  */
extern struct rd_message rc_focus_message (uint32_t frame, bool in);

/* Return the resize message of the frame FRAME, whose window has the
   size WIDTH, HEIGHT in pixels now.  A negative size becomes 0.  */
extern struct rd_message rc_resize_message (uint32_t frame, int width,
					    int height);

#endif /* REMOTE_CLIENT_INPUT_H */
