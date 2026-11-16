/* The layout and the hit test of a scroll bar that Emacs draws.

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

/* The remote display draws a vertical scroll bar as filled rectangles.
   This file calculates the rectangles of a bar, and finds the part of
   the bar at a y position.  The rules are those of the scroll bar of X
   with no toolkit (xterm.c): a border of RD_SCROLL_BAR_BORDER pixels
   on each side, and a handle of at least RD_SCROLL_BAR_MIN_HANDLE
   pixels.  The file uses no Lisp objects and no other header of Emacs,
   so the tests of the client can compile it, as they compile
   rdchunk.c.  */

#ifndef EMACS_RDSCROLLBAR_H
#define EMACS_RDSCROLLBAR_H

/* The width of the border on each side of a bar, in pixels.  */
enum { RD_SCROLL_BAR_BORDER = 2 };

/* The smallest height of a handle, in pixels, if the inside of the bar
   has that height.  */
enum { RD_SCROLL_BAR_MIN_HANDLE = 5 };

/* A rectangle.  X is relative to the left edge of the bar.  Y is in
   the coordinates of the TOP argument of rd_scroll_bar_layout.  */
struct rd_scroll_bar_rect
{
  int x;
  int y;
  int width;
  int height;
};

/* The layout of a bar.  INSIDE is the bar with no border.  ABOVE,
   HANDLE and BELOW divide INSIDE from top to bottom.  TOP is the top
   edge of the bar.  RANGE is the largest offset of a click: the inside
   height minus RD_SCROLL_BAR_MIN_HANDLE, or 0 if that is less than 0.  */
struct rd_scroll_bar_layout
{
  int top;
  int range;
  struct rd_scroll_bar_rect inside;
  struct rd_scroll_bar_rect above;
  struct rd_scroll_bar_rect handle;
  struct rd_scroll_bar_rect below;
};

/* The parts of a bar.  The order is that of the first values of enum
   scroll_bar_part in termhooks.h.  rd_scroll_bar_hit does not give
   RD_SCROLL_BAR_NOWHERE.  */
enum rd_scroll_bar_part
  {
    RD_SCROLL_BAR_NOWHERE,
    RD_SCROLL_BAR_ABOVE_HANDLE,
    RD_SCROLL_BAR_HANDLE,
    RD_SCROLL_BAR_BELOW_HANDLE,
  };

/* Return the layout of a bar with the top edge TOP, HEIGHT pixels and
   WIDTH pixels.  The window shows PORTION of WHOLE from POSITION, as
   in set_vertical_scroll_bar_hook.  A WHOLE of 0 gives a handle that
   fills the inside.  A HEIGHT or a WIDTH smaller than the borders
   gives an inside with no pixels.  */
extern struct rd_scroll_bar_layout rd_scroll_bar_layout (int top, int height,
							  int width,
							  int portion,
							  int whole,
							  int position);

/* Return the part of the bar LAYOUT at the y position Y.  Set *OFFSET
   to Y relative to the inside top, clamped to [0, range], and set
   *RANGE to the range of LAYOUT.  Y can be outside the bar: a Y above
   the bar gives RD_SCROLL_BAR_ABOVE_HANDLE, and a Y below it gives
   RD_SCROLL_BAR_BELOW_HANDLE.  */
extern enum rd_scroll_bar_part rd_scroll_bar_hit
  (const struct rd_scroll_bar_layout *layout, int y, int *offset,
   int *range);

#endif /* EMACS_RDSCROLLBAR_H */
