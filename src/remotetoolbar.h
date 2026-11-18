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

struct frame;

/* The handler of the `tool-bar-lines' frame parameter.  Set the
   height of the tool bar of F to VALUE lines.  A VALUE that is not a
   natural number gives no tool bar.  OLDVAL is the old value.  */
extern void remote_set_tool_bar_lines (struct frame *f, Lisp_Object value,
				       Lisp_Object oldval);

/* The change_tool_bar_height_hook of a remote terminal.  Set the
   height of the tool bar of F to HEIGHT pixels.  */
extern void remote_change_tool_bar_height (struct frame *f, int height);

#endif /* EMACS_REMOTETOOLBAR_H */
