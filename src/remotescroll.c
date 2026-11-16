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

/* A remote frame has the vertical scroll bar of X with no toolkit,
   which Emacs draws.  The client gets no object for a bar: a bar is
   filled rectangles on the surface of the frame, so the condemn,
   redeem and judge hooks of the terminal stay NULL.  rdscrollbar.h
   calculates the rectangles.

   Redisplay calls the hook at the end of redisplay_window, before
   update_frame calls remote_flush_frame.  So the fills of the bar go
   to the client with the other draws of the frame, and need no flush
   of their own.

   The bar has the rows of the text area.  The other rows of the
   scroll-bar column are beside the tab line, the header line and the
   mode line.  These are full-width rows, so the draw of these lines
   covers the column, and the bar does not fill them.

   The hook keeps its arguments in the vertical_scroll_bar slot of the
   window, so the input can find the parts of the bar again.  No other
   code of Emacs uses that slot when the condemn, redeem and judge
   hooks are NULL.  A window that redisplay did not draw yet has no
   such arguments.

   A press on a bar gives a scroll-bar click event, and starts the
   grab of the session.  While the grab holds, the release of its
   button gives a scroll-bar click event for the window of the press,
   wherever the pointer is.  This is the implicit grab of X.  Without
   it, keyboard.c makes a drag from the bar to a pixel position, and
   scroll-bar.el cannot use that position.  The grab keeps the press
   position, and no window, so the garbage collector does not need to
   know it.

   While the grab holds, a motion of the mouse is a motion on the bar
   of the window of the press, so `track-mouse' gives
   scroll-bar-movement events, and scroll-bar-drag moves the window.
   The drag of X keeps the offset of the press from the top of the
   handle.  This file does not: the top of the handle goes to the
   pointer, as for a click of mouse-2 in the trough.  */

#include <config.h>

#include "lisp.h"
#include "dispextern.h"
#include "frame.h"
#include "keyboard.h"
#include "termhooks.h"
#include "window.h"
#include "rdscrollbar.h"
#include "remoteterm.h"
#include "remotescroll.h"

/* The input of a later step gives a part of rdscrollbar.h to Emacs as
   an enum scroll_bar_part.  */
verify ((int) RD_SCROLL_BAR_NOWHERE == scroll_bar_nowhere);
verify ((int) RD_SCROLL_BAR_ABOVE_HANDLE == scroll_bar_above_handle);
verify ((int) RD_SCROLL_BAR_HANDLE == scroll_bar_handle);
verify ((int) RD_SCROLL_BAR_BELOW_HANDLE == scroll_bar_below_handle);

/* The arguments of rd_scroll_bar_layout that a window keeps, as the
   indexes of a vector.  */
enum
  {
    LAYOUT_TOP,
    LAYOUT_HEIGHT,
    LAYOUT_WIDTH,
    LAYOUT_PORTION,
    LAYOUT_WHOLE,
    LAYOUT_POSITION,
    LAYOUT_SIZE
  };

static void draw_bar (struct frame *, int, int, int,
		      const struct rd_scroll_bar_layout *);
static void keep_layout (struct window *, const int[LAYOUT_SIZE]);
static bool kept_layout (struct window *, struct rd_scroll_bar_layout *);
static struct rd_scroll_bar_layout layout_of (const int[LAYOUT_SIZE]);
static struct window *grab_window (const struct remote_scroll_bar_grab *);
static struct window *bar_window_at (struct frame *, int, int);
static void store_click (struct window *,
			 const struct rd_scroll_bar_layout *,
			 const struct remote_scroll_bar_button *);

void
remote_set_vertical_scroll_bar (struct window *w, int portion, int whole,
				int position)
{
  struct frame *f = XFRAME (w->frame);
  int left = WINDOW_SCROLL_BAR_AREA_X (w);
  int width = WINDOW_SCROLL_BAR_AREA_WIDTH (w);
  int box_top, box_height;
  window_box (w, ANY_AREA, NULL, &box_top, NULL, &box_height);
  int args[LAYOUT_SIZE] = { box_top, box_height, width, portion, whole,
			   position };
  keep_layout (w, args);
  struct rd_scroll_bar_layout layout = layout_of (args);
  draw_bar (f, left, width, box_height, &layout);
}

bool
remote_scroll_bar_grab_release (struct remote_scroll_bar_grab *grab,
				const struct remote_scroll_bar_button *button)
{
  if (!grab->held || button->code != grab->code)
    return false;
  grab->held = false;
  if (button->press)
    return false;
  struct window *w = grab_window (grab);
  struct rd_scroll_bar_layout layout;
  if (w && kept_layout (w, &layout))
    store_click (w, &layout, button);
  return true;
}

bool
remote_scroll_bar_press (struct remote_scroll_bar_grab *grab,
			 const struct remote_scroll_bar_button *button)
{
  struct window *w = bar_window_at (button->frame, button->x, button->y);
  if (!w || !button->press)
    return false;
  struct rd_scroll_bar_layout layout;
  if (kept_layout (w, &layout))
    {
      *grab = (struct remote_scroll_bar_grab)
	{ .held = true, .frame = button->frame, .x = button->x,
	  .y = button->y, .code = button->code };
      store_click (w, &layout, button);
    }
  return true;
}

bool
remote_scroll_bar_motion (const struct remote_scroll_bar_grab *grab, int y,
			  struct frame **fp, Lisp_Object *bar_window,
			  enum scroll_bar_part *part, Lisp_Object *offset,
			  Lisp_Object *range)
{
  struct window *w = grab->held ? grab_window (grab) : NULL;
  struct rd_scroll_bar_layout layout;
  if (!w || !kept_layout (w, &layout))
    return false;
  int bar_offset, bar_range;
  *part = (enum scroll_bar_part) rd_scroll_bar_hit (&layout, y, &bar_offset,
						    &bar_range);
  *fp = grab->frame;
  XSETWINDOW (*bar_window, w);
  XSETINT (*offset, bar_offset);
  XSETINT (*range, bar_range);
  return true;
}

void
remote_scroll_bar_forget_frame (struct remote_scroll_bar_grab *grab,
				struct frame *f)
{
  if (grab->frame == f)
    grab->frame = NULL;
}

/* Draw the bar LAYOUT at LEFT on F, with the size WIDTH, HEIGHT: the
   trough and the border in the frame background, and then the handle
   in the frame foreground.  */
static void
draw_bar (struct frame *f, int left, int width, int height,
	  const struct rd_scroll_bar_layout *layout)
{
  const struct rd_scroll_bar_rect *handle = &layout->handle;
  remote_fill_area (f, left, layout->top, width, height,
		    FRAME_BACKGROUND_PIXEL (f));
  remote_fill_area (f, left + handle->x, handle->y, handle->width,
		    handle->height, FRAME_FOREGROUND_PIXEL (f));
}

/* Keep ARGS, the arguments of the layout of the bar of W, in W.  Use
   the vector that W has, if it has one.  */
static void
keep_layout (struct window *w, const int args[LAYOUT_SIZE])
{
  Lisp_Object kept = w->vertical_scroll_bar;
  if (!(VECTORP (kept) && ASIZE (kept) == LAYOUT_SIZE))
    {
      kept = make_nil_vector (LAYOUT_SIZE);
      wset_vertical_scroll_bar (w, kept);
    }
  for (int i = 0; i < LAYOUT_SIZE; i++)
    ASET (kept, i, make_fixnum (args[i]));
}

/* Set *LAYOUT to the layout of the bar of W from the arguments that W
   keeps, and return true.  Return false if W keeps no arguments.  */
static bool
kept_layout (struct window *w, struct rd_scroll_bar_layout *layout)
{
  Lisp_Object kept = w->vertical_scroll_bar;
  if (!(VECTORP (kept) && ASIZE (kept) == LAYOUT_SIZE))
    return false;
  int args[LAYOUT_SIZE];
  for (int i = 0; i < LAYOUT_SIZE; i++)
    args[i] = XFIXNUM (AREF (kept, i));
  *layout = layout_of (args);
  return true;
}

/* Return the layout of a bar with the arguments ARGS.  */
static struct rd_scroll_bar_layout
layout_of (const int args[LAYOUT_SIZE])
{
  return rd_scroll_bar_layout (args[LAYOUT_TOP], args[LAYOUT_HEIGHT],
			       args[LAYOUT_WIDTH], args[LAYOUT_PORTION],
			       args[LAYOUT_WHOLE], args[LAYOUT_POSITION]);
}

/* Return the window of the press of GRAB, or NULL if the frame of the
   press was deleted or no bar is at the press position now.  */
static struct window *
grab_window (const struct remote_scroll_bar_grab *grab)
{
  return (grab->frame
	  ? bar_window_at (grab->frame, grab->x, grab->y)
	  : NULL);
}

/* Return the window whose vertical scroll bar is at X, Y of F, or NULL
   if no bar is there.  */
static struct window *
bar_window_at (struct frame *f, int x, int y)
{
  enum window_part part;
  Lisp_Object window = window_from_coordinates (f, x, y, &part, false,
						false, false);
  return (WINDOWP (window) && part == ON_VERTICAL_SCROLL_BAR
	  ? XWINDOW (window) : NULL);
}

/* Store the scroll-bar click event of BUTTON on the bar LAYOUT of W,
   as x_scroll_bar_handle_click of xterm.c does.  The x of the event is
   the offset of BUTTON in the bar, and the y is the range of the
   bar.  */
static void
store_click (struct window *w, const struct rd_scroll_bar_layout *layout,
	     const struct remote_scroll_bar_button *button)
{
  int offset, range;
  enum rd_scroll_bar_part part = rd_scroll_bar_hit (layout, button->y,
						    &offset, &range);
  struct input_event ie;
  EVENT_INIT (ie);
  ie.kind = SCROLL_BAR_CLICK_EVENT;
  ie.code = button->code;
  ie.modifiers = (button->modifiers
		  | (button->press ? down_modifier : up_modifier));
  XSETWINDOW (ie.frame_or_window, w);
  ie.arg = Qnil;
  ie.timestamp = button->time;
  ie.part = (enum scroll_bar_part) part;
  XSETINT (ie.x, offset);
  XSETINT (ie.y, range);
  kbd_buffer_store_event (&ie);
}
