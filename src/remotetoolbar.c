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

/* A remote frame has the tool bar that Emacs draws: the tool-bar
   window of the frame, above the windows.  Redisplay draws the items
   of the tool bar into that window, and the icons are image glyphs.
   This file sets the height of the tool bar, and gives the buttons
   in the tool bar to it.  The model is the tool bar of Android
   (androidfns.c and androidterm.c).  */

#include <config.h>

#include "lisp.h"
#include "dispextern.h"
#include "frame.h"
#include "window.h"
#include "remotetoolbar.h"

static void remote_resize_for_tool_bar (struct frame *f);
static bool remote_tool_bar_takes_p (struct frame *f, int x, int y,
				     bool press);

void
remote_set_tool_bar_lines (struct frame *f, Lisp_Object value,
			   Lisp_Object oldval)
{
  /* A frame with only a minibuffer has no tool bar, as it has no menu
     bar.  */
  if (FRAME_MINIBUF_ONLY_P (f))
    return;

  int lines = RANGED_FIXNUMP (0, value, INT_MAX) ? XFIXNAT (value) : 0;
  remote_change_tool_bar_height (f, lines * FRAME_LINE_HEIGHT (f));
}

/* Set the height of the tool bar of F to HEIGHT pixels, and store the
   number of lines in the `tool-bar-lines' parameter.  Then give the
   windows of F the remaining height, and mark F for a full
   redisplay.  */
void
remote_change_tool_bar_height (struct frame *f, int height)
{
  int unit = FRAME_LINE_HEIGHT (f);
  int old_height = FRAME_TOOL_BAR_HEIGHT (f);
  int lines = (height + unit - 1) / unit;

  fset_redisplay (f);
  FRAME_TOOL_BAR_HEIGHT (f) = height;
  FRAME_TOOL_BAR_LINES (f) = lines;
  store_frame_param (f, Qtool_bar_lines, make_fixnum (lines));

  /* The rows below the new height must not stay in the matrix of the
     tool-bar window.  */
  if (height < old_height && WINDOWP (f->tool_bar_window))
    clear_glyph_matrix (XWINDOW (f->tool_bar_window)->current_matrix);

  remote_resize_for_tool_bar (f);
  adjust_frame_glyphs (f);
  SET_FRAME_GARBAGED (f);
}

/* Change the size of F for a new tool bar height.  Until redisplay
   drew the tool bar (tool_bar_resized is false), keep the text size,
   so the native height changes.  This includes the start of F, where
   frame_inhibit_resize ignores `frame-inhibit-implied-resize'.  After
   that, keep the native size if `frame-inhibit-implied-resize' asks
   for it.  A frame with a full-screen height keeps its native
   height.  */
static void
remote_resize_for_tool_bar (struct frame *f)
{
  if (f->tool_bar_resized)
    {
      adjust_frame_size (f, -1, -1, 3, false, Qtool_bar_lines);
      return;
    }

  Lisp_Object fullscreen = get_frame_param (f, Qfullscreen);
  if (NILP (fullscreen) || EQ (fullscreen, Qfullwidth))
    adjust_frame_size (f, FRAME_TEXT_WIDTH (f), FRAME_TEXT_HEIGHT (f),
		       1, false, Qtool_bar_lines);
  else
    adjust_frame_size (f, -1, -1, 4, false, Qtool_bar_lines);
  f->tool_bar_resized = f->tool_bar_redisplayed;
}

bool
remote_tool_bar_button (struct frame *f, int x, int y, int button,
			bool press, int modifiers)
{
  /* As on Android, the tool bar takes only the buttons 1 to 3.  The
     buttons 4 to 7 are the wheel, and keep the wheel events.  */
  if (button >= 4 || !remote_tool_bar_takes_p (f, x, y, press))
    return false;
  handle_tool_bar_click (f, x, y, press, modifiers);
  return true;
}

/* Return true if the tool bar of F takes a button at X, Y.  PRESS is
   true for a press.  The tool bar takes a button only in the tool-bar
   window.  It takes a release only after a press on an item
   (last_tool_bar_item), so that a press in the text area and a
   release in the tool bar give a normal click.  */
static bool
remote_tool_bar_takes_p (struct frame *f, int x, int y, bool press)
{
  if (!WINDOWP (f->tool_bar_window)
      || !WINDOW_TOTAL_LINES (XWINDOW (f->tool_bar_window)))
    return false;
  Lisp_Object window = window_from_coordinates (f, x, y, 0, true, true,
						true);
  return (EQ (window, f->tool_bar_window)
	  && (press || f->last_tool_bar_item != -1));
}
