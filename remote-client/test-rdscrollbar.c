/* Test the layout and the hit test of a scroll bar.

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

/* The laws of ../src/rdscrollbar.c, for random bars.  Some bars are
   smaller than their borders, some have a WHOLE of 0, and some have
   values near INT_MAX.

   - Tiling: the inside is the bar with no border (WIDTH - 4 and
     HEIGHT - 4, or 0).  ABOVE, HANDLE and BELOW have the x and the
     width of the inside, do not overlap, and together are exactly the
     inside, from top to bottom.
   - Visible handle: the handle height is at least
     min (RD_SCROLL_BAR_MIN_HANDLE, inside height).
   - Monotone position: a larger POSITION, with the same PORTION and
     WHOLE, does not move the handle top up.
   - Whole zero: a WHOLE of 0 gives a handle that fills the inside.
   - Handle proportion: if WHOLE is not 0, the handle has the rows of
     an oracle with 64-bit integers.  The handle starts at
     clamp (POSITION * range / WHOLE, 0, range) and stops at
     max (start, clamp ((POSITION + PORTION) * range / WHOLE, 0, range))
     + RD_SCROLL_BAR_MIN_HANDLE, and the inside clips the two rows.
   - Monotone portion: a larger PORTION, with the same POSITION and
     WHOLE, does not make the handle smaller.
   - Full view: if WHOLE is not 0, POSITION is 0 and PORTION is at least
     WHOLE, the handle fills the inside.
   - Hit agrees: the first row, the last row and a random row of ABOVE,
     HANDLE and BELOW give the part of that rectangle.  The row above a
     handle gives ABOVE, and the row below it gives BELOW.
   - Hit monotone: a larger y does not give a part or an offset that is
     smaller.
   - Offset: for each y, also above and below the bar, the offset is
     clamp (y - TOP - 2, 0, range), and the range is
     max (0, inside height - RD_SCROLL_BAR_MIN_HANDLE).

   The examples test a bar of height 0 and bars smaller than their
   borders.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "rdscrollbar.h"

enum { TRIALS = 2000 };

/* The arguments of rd_scroll_bar_layout.  */
struct bar
{
  int top;
  int height;
  int width;
  int portion;
  int whole;
  int position;
};

static const char *run_trial (uint64_t);
static bool tiling_law (uint64_t *);
static bool visible_handle_law (uint64_t *);
static bool monotone_position_law (uint64_t *);
static bool whole_zero_law (uint64_t *);
static bool handle_proportion_law (uint64_t *);
static bool monotone_portion_law (uint64_t *);
static bool full_view_law (uint64_t *);
static bool hit_agrees_law (uint64_t *);
static bool hit_monotone_law (uint64_t *);
static bool offset_law (uint64_t *);
static bool small_bar_example (uint64_t *);
static int oracle_row (int64_t, int, int);
static bool tiles_p (const struct rd_scroll_bar_layout *, const struct bar *);
static bool hit_in_p (const struct rd_scroll_bar_layout *,
		      const struct rd_scroll_bar_rect *,
		      enum rd_scroll_bar_part, uint64_t *);
static bool hit_beside_handle_p (const struct rd_scroll_bar_layout *);
static struct rd_scroll_bar_layout layout_of (const struct bar *);
static enum rd_scroll_bar_part hit (const struct rd_scroll_bar_layout *, int,
				    int *);
static struct bar random_bar (uint64_t *);
static int random_size (uint64_t *);
static int random_amount (uint64_t *);
static int random_y (uint64_t *, const struct bar *);
static int clamp (int, int, int);
static int max_int (int, int);
static int min_int (int, int);
static int random_between (uint64_t *, int, int);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "tiling", tiling_law },
    { "visible handle", visible_handle_law },
    { "monotone position", monotone_position_law },
    { "whole zero", whole_zero_law },
    { "handle proportion", handle_proportion_law },
    { "monotone portion", monotone_portion_law },
    { "full view", full_view_law },
    { "hit agrees", hit_agrees_law },
    { "hit monotone", hit_monotone_law },
    { "offset", offset_law },
    { "small bar", small_bar_example },
  };

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 1 : 0;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    {
      const char *failed = run_trial (base + trial);
      if (failed)
	{
	  fprintf (stderr,
		   "test-rdscrollbar: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-rdscrollbar: %d trials passed\n", TRIALS);
  return 0;
}

/* Run each law with SEED.  Return the name of the first law that
   fails, or NULL.  */
static const char *
run_trial (uint64_t seed)
{
  for (size_t i = 0; i < sizeof laws / sizeof *laws; i++)
    {
      uint64_t state = seed;
      if (!laws[i].check (&state))
	return laws[i].name;
    }
  return NULL;
}

static bool
tiling_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  struct rd_scroll_bar_layout layout = layout_of (&bar);
  return tiles_p (&layout, &bar);
}

static bool
visible_handle_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  struct rd_scroll_bar_layout layout = layout_of (&bar);
  return (layout.handle.height
	  >= min_int (RD_SCROLL_BAR_MIN_HANDLE, layout.inside.height));
}

static bool
monotone_position_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  struct bar later = bar;
  later.position = (bar.position
		    + random_below (state, (uint64_t) INT_MAX - bar.position
				    + 1));
  struct rd_scroll_bar_layout first = layout_of (&bar);
  struct rd_scroll_bar_layout second = layout_of (&later);
  return second.handle.y >= first.handle.y;
}

static bool
whole_zero_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  bar.whole = 0;
  struct rd_scroll_bar_layout layout = layout_of (&bar);
  return (layout.handle.y == layout.inside.y
	  && layout.handle.height == layout.inside.height);
}

static bool
handle_proportion_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  if (bar.whole == 0)
    bar.whole = 1;
  struct rd_scroll_bar_layout layout = layout_of (&bar);
  int range = layout.range, height = layout.inside.height;
  int start = oracle_row (bar.position, range, bar.whole);
  int end = max_int (start, oracle_row ((int64_t) bar.position + bar.portion,
					range, bar.whole));
  int top = min_int (start, height);
  return (layout.handle.y - layout.inside.y == top
	  && (layout.handle.height
	      == min_int (end + RD_SCROLL_BAR_MIN_HANDLE, height) - top));
}

static bool
monotone_portion_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  struct bar larger = bar;
  larger.portion = (bar.portion
		    + random_below (state, (uint64_t) INT_MAX - bar.portion
				    + 1));
  struct rd_scroll_bar_layout first = layout_of (&bar);
  struct rd_scroll_bar_layout second = layout_of (&larger);
  return second.handle.height >= first.handle.height;
}

static bool
full_view_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  if (bar.whole == 0)
    bar.whole = 1;
  bar.position = 0;
  bar.portion = (bar.whole
		 + random_below (state, (uint64_t) INT_MAX - bar.whole + 1));
  struct rd_scroll_bar_layout layout = layout_of (&bar);
  return (layout.handle.y == layout.inside.y
	  && layout.handle.height == layout.inside.height);
}

static bool
hit_agrees_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  struct rd_scroll_bar_layout layout = layout_of (&bar);
  return (hit_in_p (&layout, &layout.above, RD_SCROLL_BAR_ABOVE_HANDLE, state)
	  && hit_in_p (&layout, &layout.handle, RD_SCROLL_BAR_HANDLE, state)
	  && hit_in_p (&layout, &layout.below, RD_SCROLL_BAR_BELOW_HANDLE,
		       state)
	  && hit_beside_handle_p (&layout));
}

static bool
hit_monotone_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  struct rd_scroll_bar_layout layout = layout_of (&bar);
  int y1 = random_y (state, &bar);
  int y2 = random_y (state, &bar);
  int low = min_int (y1, y2), high = max_int (y1, y2);
  int low_offset, high_offset;
  enum rd_scroll_bar_part low_part = hit (&layout, low, &low_offset);
  enum rd_scroll_bar_part high_part = hit (&layout, high, &high_offset);
  return low_part <= high_part && low_offset <= high_offset;
}

static bool
offset_law (uint64_t *state)
{
  struct bar bar = random_bar (state);
  struct rd_scroll_bar_layout layout = layout_of (&bar);
  int y = random_y (state, &bar);
  int range = max_int (0, layout.inside.height - RD_SCROLL_BAR_MIN_HANDLE);
  int offset, given_range;
  rd_scroll_bar_hit (&layout, y, &offset, &given_range);
  return (layout.range == range && given_range == range
	  && offset == clamp (y - bar.top - RD_SCROLL_BAR_BORDER, 0, range));
}

/* A bar with HEIGHT from 0 to 2 * RD_SCROLL_BAR_BORDER + 1, and a WIDTH
   from 0 to 2 * RD_SCROLL_BAR_BORDER + 1.  An inside with no pixels
   has three rectangles with no pixels, a range of 0, and an offset of 0
   for each y.  */
static bool
small_bar_example (uint64_t *state)
{
  bool passed = true;
  for (int height = 0; passed && height <= 2 * RD_SCROLL_BAR_BORDER + 1;
       height++)
    for (int width = 0; passed && width <= 2 * RD_SCROLL_BAR_BORDER + 1;
	 width++)
      {
	struct bar bar = random_bar (state);
	bar.height = height;
	bar.width = width;
	struct rd_scroll_bar_layout layout = layout_of (&bar);
	int inside_height = max_int (0, height - 2 * RD_SCROLL_BAR_BORDER);
	int inside_width = max_int (0, width - 2 * RD_SCROLL_BAR_BORDER);
	int offset;
	hit (&layout, random_y (state, &bar), &offset);
	passed = (tiles_p (&layout, &bar)
		  && layout.inside.height == inside_height
		  && layout.inside.width == inside_width
		  && layout.handle.height == inside_height
		  && layout.range == 0 && offset == 0);
      }
  return passed;
}

/* Return true if LAYOUT is the layout of the tiling law for BAR.  */
static bool
tiles_p (const struct rd_scroll_bar_layout *layout, const struct bar *bar)
{
  const struct rd_scroll_bar_rect *inside = &layout->inside;
  const struct rd_scroll_bar_rect *parts[]
    = { &layout->above, &layout->handle, &layout->below };
  bool passed
    = (layout->top == bar->top
       && inside->x == RD_SCROLL_BAR_BORDER
       && inside->y == bar->top + RD_SCROLL_BAR_BORDER
       && (inside->width
	   == max_int (0, bar->width - 2 * RD_SCROLL_BAR_BORDER))
       && (inside->height
	   == max_int (0, bar->height - 2 * RD_SCROLL_BAR_BORDER)));
  int next = inside->y;
  for (int i = 0; passed && i < 3; i++)
    {
      passed = (parts[i]->x == inside->x && parts[i]->width == inside->width
		&& parts[i]->y == next && parts[i]->height >= 0);
      next += parts[i]->height;
    }
  return passed && next == inside->y + inside->height;
}

/* Return the row that the oracle of the handle proportion law gives
   for AMOUNT in a bar with RANGE and WHOLE: AMOUNT * RANGE / WHOLE,
   rounded down and clamped to [0, RANGE].  The product of two values
   of int fits in 64 bits.  */
static int
oracle_row (int64_t amount, int range, int whole)
{
  int64_t row = amount * range / whole;
  return row > range ? range : (int) row;
}

/* Return true if the first row, the last row and a random row of RECT
   give PART in LAYOUT.  A RECT with no pixels has no rows, so it gives
   true.  */
static bool
hit_in_p (const struct rd_scroll_bar_layout *layout,
	  const struct rd_scroll_bar_rect *rect, enum rd_scroll_bar_part part,
	  uint64_t *state)
{
  if (rect->height == 0)
    return true;
  int rows[] = { rect->y, rect->y + rect->height - 1,
		 rect->y + (int) random_below (state, rect->height) };
  int offset;
  for (size_t i = 0; i < sizeof rows / sizeof *rows; i++)
    if (hit (layout, rows[i], &offset) != part)
      return false;
  return true;
}

/* Return true if the row above the handle of LAYOUT gives
   RD_SCROLL_BAR_ABOVE_HANDLE, and the row below it gives
   RD_SCROLL_BAR_BELOW_HANDLE.  A handle with no pixels gives true.  */
static bool
hit_beside_handle_p (const struct rd_scroll_bar_layout *layout)
{
  const struct rd_scroll_bar_rect *handle = &layout->handle;
  int offset;
  return (handle->height == 0
	  || (hit (layout, handle->y - 1, &offset)
	      == RD_SCROLL_BAR_ABOVE_HANDLE
	      && (hit (layout, handle->y + handle->height, &offset)
		  == RD_SCROLL_BAR_BELOW_HANDLE)));
}

/* Return the layout of BAR.  */
static struct rd_scroll_bar_layout
layout_of (const struct bar *bar)
{
  return rd_scroll_bar_layout (bar->top, bar->height, bar->width,
			       bar->portion, bar->whole, bar->position);
}

/* Return the part of LAYOUT at Y, and set *OFFSET.  */
static enum rd_scroll_bar_part
hit (const struct rd_scroll_bar_layout *layout, int y, int *offset)
{
  int range;
  return rd_scroll_bar_hit (layout, y, offset, &range);
}

/* Return a random bar.  PORTION and POSITION can be larger than WHOLE,
   as near the end of a buffer.  */
static struct bar
random_bar (uint64_t *state)
{
  struct bar bar;
  bar.top = random_between (state, -100, 4000);
  bar.height = random_size (state);
  bar.width = random_between (state, 0, 40);
  bar.whole = random_below (state, 8) == 0 ? 0 : random_amount (state);
  bar.portion = random_amount (state);
  bar.position = random_amount (state);
  return bar;
}

/* Return a random height: often one near the borders and the smallest
   handle, else one up to the height of a large screen.  */
static int
random_size (uint64_t *state)
{
  return (random_below (state, 3) == 0
	  ? random_between (state, 0, 4 * RD_SCROLL_BAR_MIN_HANDLE)
	  : random_between (state, 0, 4000));
}

/* Return a random amount for PORTION, WHOLE or POSITION: a small one, a
   size of a buffer, or one near INT_MAX.  */
static int
random_amount (uint64_t *state)
{
  switch (random_below (state, 3))
    {
    case 0:
      return random_between (state, 0, 100);
    case 1:
      return random_between (state, 0, 10000000);
    default:
      return INT_MAX - random_between (state, 0, 1000);
    }
}

/* Return a random y in BAR, or near it, or far from it.  */
static int
random_y (uint64_t *state, const struct bar *bar)
{
  return (random_below (state, 8) == 0
	  ? random_between (state, -100000, 100000)
	  : random_between (state, bar->top - 20,
			    bar->top + bar->height + 20));
}

/* Return VALUE clamped to [LOW, HIGH].  */
static int
clamp (int value, int low, int high)
{
  return value < low ? low : value > high ? high : value;
}

static int
max_int (int a, int b)
{
  return a > b ? a : b;
}

static int
min_int (int a, int b)
{
  return a < b ? a : b;
}

/* Return a random number from LOW to HIGH.  */
static int
random_between (uint64_t *state, int low, int high)
{
  return low + (int) random_below (state, (uint64_t) high - low + 1);
}

/* Return a random number from 0 to LIMIT - 1.  */
static uint64_t
random_below (uint64_t *state, uint64_t limit)
{
  return random_next (state) % limit;
}

/* Return the next number of the generator splitmix64.  */
static uint64_t
random_next (uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}
