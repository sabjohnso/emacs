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

#include <config.h>

#include "rdscrollbar.h"

static struct rd_scroll_bar_rect inside_of (int, int, int);
static void handle_rows (int, int, int, int, int *, int *);
static int scale (double, int, int);
static struct rd_scroll_bar_rect rows_of (const struct rd_scroll_bar_rect *,
					  int, int);
static int clamp (int, int, int);
static int max (int, int);
static int min (int, int);

struct rd_scroll_bar_layout
rd_scroll_bar_layout (int top, int height, int width, int portion, int whole,
		      int position)
{
  struct rd_scroll_bar_rect inside = inside_of (top, height, width);
  int range = max (0, inside.height - RD_SCROLL_BAR_MIN_HANDLE);
  int start, end;
  handle_rows (range, portion, whole, position, &start, &end);
  return (struct rd_scroll_bar_layout)
    { .top = top,
      .range = range,
      .inside = inside,
      .above = rows_of (&inside, 0, start),
      .handle = rows_of (&inside, start, end),
      .below = rows_of (&inside, end, inside.height) };
}

enum rd_scroll_bar_part
rd_scroll_bar_hit (const struct rd_scroll_bar_layout *layout, int y,
		   int *offset, int *range)
{
  *range = layout->range;
  *offset = clamp (y - layout->inside.y, 0, layout->range);
  if (y < layout->handle.y)
    return RD_SCROLL_BAR_ABOVE_HANDLE;
  if (y < layout->handle.y + layout->handle.height)
    return RD_SCROLL_BAR_HANDLE;
  return RD_SCROLL_BAR_BELOW_HANDLE;
}

/* Return the inside of a bar at TOP with HEIGHT and WIDTH: the bar with
   no border.  */
static struct rd_scroll_bar_rect
inside_of (int top, int height, int width)
{
  return (struct rd_scroll_bar_rect)
    { .x = RD_SCROLL_BAR_BORDER,
      .y = top + RD_SCROLL_BAR_BORDER,
      .width = max (0, width - 2 * RD_SCROLL_BAR_BORDER),
      .height = max (0, height - 2 * RD_SCROLL_BAR_BORDER) };
}

/* Set *START and *END to the first row of the handle and the row after
   it, relative to the inside top, for a bar with RANGE.  These are the
   rules of xterm.c (x_scroll_bar_set_handle): the start stays in
   [0, RANGE], the end stays in [start, RANGE], and the handle goes
   RD_SCROLL_BAR_MIN_HANDLE rows past the end.  The inside height is
   at least RANGE + RD_SCROLL_BAR_MIN_HANDLE if RANGE is not 0, so only
   an inside with less than RD_SCROLL_BAR_MIN_HANDLE rows clips the
   handle.  */
static void
handle_rows (int range, int portion, int whole, int position, int *start,
	     int *end)
{
  if (whole == 0)
    {
      *start = 0;
      *end = range;
    }
  else
    {
      *start = scale (position, range, whole);
      *end = max (*start, scale ((double) position + portion, range, whole));
    }
  *end += RD_SCROLL_BAR_MIN_HANDLE;
}

/* Return AMOUNT * RANGE / WHOLE, clamped to [0, RANGE].  The product
   is a double, so a large AMOUNT does not overflow.  */
static int
scale (double amount, int range, int whole)
{
  double value = amount * range / whole;
  return value < 0 ? 0 : value > range ? range : (int) value;
}

/* Return the rows from FIRST to LAST (not included) of INSIDE, relative
   to the inside top.  A row after the inside goes to its last row.  */
static struct rd_scroll_bar_rect
rows_of (const struct rd_scroll_bar_rect *inside, int first, int last)
{
  int low = min (first, inside->height);
  int high = min (last, inside->height);
  return (struct rd_scroll_bar_rect)
    { .x = inside->x,
      .y = inside->y + low,
      .width = inside->width,
      .height = high - low };
}

/* Return VALUE clamped to [LOW, HIGH].  */
static int
clamp (int value, int low, int high)
{
  return value < low ? low : value > high ? high : value;
}

static int
max (int a, int b)
{
  return a > b ? a : b;
}

static int
min (int a, int b)
{
  return a < b ? a : b;
}
