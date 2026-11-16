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
   CODE.  FRAME is NULL if the frame was deleted after the press.  */
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

/* If GRAB holds and BUTTON is a release of its button, end GRAB,
   store the scroll-bar event of the release for the window of the
   press, and return true.  The release can be in any frame.  A press
   of the button of GRAB ends GRAB too, as a release that the client
   lost, but it returns false, because the press is a new press.
   Return false for each other button.  */
extern bool remote_scroll_bar_grab_release
  (struct remote_scroll_bar_grab *grab,
   const struct remote_scroll_bar_button *button);

/* If BUTTON is a press on a vertical scroll bar, start GRAB, store the
   scroll-bar event of the press, and return true.  A press on a bar
   that redisplay did not draw yet has no event, but the function
   returns true for it.  Return false for each other button.  */
extern bool remote_scroll_bar_press
  (struct remote_scroll_bar_grab *grab,
   const struct remote_scroll_bar_button *button);

/* Remove F from GRAB.  Call this when F is deleted.  */
extern void remote_scroll_bar_forget_frame
  (struct remote_scroll_bar_grab *grab, struct frame *f);

#endif /* EMACS_REMOTESCROLL_H */
