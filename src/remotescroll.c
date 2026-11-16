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
   covers the column, and the bar does not fill them.  */

#include <config.h>

#include "lisp.h"
#include "dispextern.h"
#include "frame.h"
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

static void draw_bar (struct frame *, int, int, int,
		      const struct rd_scroll_bar_layout *);

void
remote_set_vertical_scroll_bar (struct window *w, int portion, int whole,
				int position)
{
  struct frame *f = XFRAME (w->frame);
  int left = WINDOW_SCROLL_BAR_AREA_X (w);
  int width = WINDOW_SCROLL_BAR_AREA_WIDTH (w);
  int box_top, box_height;
  window_box (w, ANY_AREA, NULL, &box_top, NULL, &box_height);
  struct rd_scroll_bar_layout layout
    = rd_scroll_bar_layout (box_top, box_height, width, portion, whole,
			    position);
  draw_bar (f, left, width, box_height, &layout);
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
