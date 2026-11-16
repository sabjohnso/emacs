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

struct window;

/* The set_vertical_scroll_bar_hook of a remote terminal.  Draw the
   scroll bar of W beside the text area of W.  W shows PORTION of WHOLE
   from POSITION.  */
extern void remote_set_vertical_scroll_bar (struct window *w, int portion,
					    int whole, int position);

#endif /* EMACS_REMOTESCROLL_H */
