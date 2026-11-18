/* The tool bar of a remote frame.

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

#ifndef EMACS_REMOTETOOLBAR_H
#define EMACS_REMOTETOOLBAR_H

#include "lisp.h"
#include "dispextern.h"

struct frame;
struct glyph_string;

/* The handler of the `tool-bar-lines' frame parameter.  Set the
   height of the tool bar of F to VALUE lines.  A VALUE that is not a
   natural number gives no tool bar.  OLDVAL, the old value, is not
   used.  */
extern void remote_set_tool_bar_lines (struct frame *f, Lisp_Object value,
				       Lisp_Object oldval);

/* The change_tool_bar_height_hook of a remote terminal.  Set the
   height of the tool bar of F to HEIGHT pixels.  */
extern void remote_change_tool_bar_height (struct frame *f, int height);

/* Give the button BUTTON at X, Y of F to the tool bar of F, if the
   tool bar takes it.  PRESS is true for a press and false for a
   release, and MODIFIERS are the Emacs modifiers.  The tool bar
   takes the buttons 1 to 3 in the tool-bar window.  It takes a
   release only after a press on an item.  Return true if the tool bar
   took the button, and false if not.  */
extern bool remote_tool_bar_button (struct frame *f, int x, int y,
				    int button, bool press, int modifiers);

/* Draw the relief of S, a string of one slice of an image, if it has
   one.  An image has a relief if it has the `:relief' property, or if
   S is a tool-bar button with the raised or the sunken highlight.
   SLICE is the rectangle of the slice in the frame, and the fills
   change only the pixels in CLIP.  */
extern void remote_draw_image_relief (struct glyph_string *s,
				      Emacs_Rectangle slice,
				      Emacs_Rectangle clip);

#endif /* EMACS_REMOTETOOLBAR_H */
