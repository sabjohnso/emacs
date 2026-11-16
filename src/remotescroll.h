/* The vertical scroll bar of a window of a remote frame.

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

#ifndef EMACS_REMOTESCROLL_H
#define EMACS_REMOTESCROLL_H

#include "dispextern.h"
#include "termhooks.h"

struct window;
struct frame;

/* A press or a release of a mouse button that is not a wheel button.
   FRAME is the frame of the message, and X, Y is the position in it.
   CODE is the button number minus 1.  MODIFIERS are the modifiers of
   Emacs, with no down or up modifier.  */
struct remote_scroll_bar_button
{
  struct frame *frame;
  int x;
  int y;
  int code;
  bool press;
  int modifiers;
  Time time;
};

/* The grab of a session.  A press on a bar starts the grab, and the
   release of that button ends it, as the implicit grab of X does.
   While HELD is true, the press was at X, Y of FRAME with the button
   CODE.  FRAME is NULL if the frame was deleted after the press.

   The session keeps the grab in its struct remote_display_info, but
   only remotescroll.c reads or writes the fields.  Other code gives
   the grab to the functions of this file.  */
struct remote_scroll_bar_grab
{
  bool held;
  struct frame *frame;
  int x;
  int y;
  int code;
};

/* The set_vertical_scroll_bar_hook of a remote terminal.  Draw the
   scroll bar of W beside the text area of W.  W shows PORTION of WHOLE
   from POSITION.  */
extern void remote_set_vertical_scroll_bar (struct window *w, int portion,
					    int whole, int position);

/* Give BUTTON to the scroll bars, and return true if a scroll bar
   used it.  WHEEL is true if BUTTON is a wheel button.

   - A release of the button of GRAB, in any frame, ends GRAB.  It
     stores a scroll-bar event for the window of the press, if a bar
     with a layout is at the press position now.
   - A press of the button of GRAB also ends GRAB, as a release that
     the client lost.  Then the press is a new press.
   - A press on a vertical scroll bar starts GRAB, and stores a
     scroll-bar event if redisplay drew the bar.  A wheel button does
     not press a bar.

   Return false for each other button.  */
extern bool remote_scroll_bar_button_input
  (struct remote_scroll_bar_grab *grab,
   const struct remote_scroll_bar_button *button, bool wheel);

/* If GRAB holds, report a motion of the mouse to the row Y as a motion
   on the bar of the window of the press, and return true.  This is
   modelled on x_scroll_bar_report_motion of xterm.c.  Set *FP to the
   frame of the press, *BAR_WINDOW to the window, and *PART to the part
   of the bar at Y.  Set *OFFSET to Y relative to the inside top of the
   bar, clamped to [0, range], and *RANGE to the range of the bar.
   Return false, and change nothing, if GRAB does not hold, the frame
   of the press was deleted, or no bar with a layout is at the press
   position now.  */
extern bool remote_scroll_bar_motion
  (const struct remote_scroll_bar_grab *grab, int y, struct frame **fp,
   Lisp_Object *bar_window, enum scroll_bar_part *part,
   Lisp_Object *offset, Lisp_Object *range);

/* Remove F from GRAB.  Call this when F is deleted.  */
extern void remote_scroll_bar_forget_frame
  (struct remote_scroll_bar_grab *grab, struct frame *f);

#endif /* EMACS_REMOTESCROLL_H */
