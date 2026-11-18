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
   This file sets the height of the tool bar, gives the buttons in the
   tool bar to it, and draws the relief of the buttons.  The model is
   the tool bar of Android (androidfns.c and androidterm.c), and the
   relief of pgtk (pgtkterm.c).  */

#include <config.h>

#include "lisp.h"
#include "dispextern.h"
#include "frame.h"
#include "window.h"
#include "remoteterm.h"
#include "remotetoolbar.h"

static void remote_resize_for_tool_bar (struct frame *f);
static bool remote_tool_bar_takes_p (struct frame *f, int x, int y,
				     bool press);

/* The relief of an image: the width of the sides, the type, and the
   space between the image and the sides.  */
struct remote_relief
{
  int thick;
  bool raised;
  int extra_x, extra_y;
};

/* The two colors of a relief.  */
struct remote_relief_colors
{
  unsigned long light, dark;
};

static struct remote_relief remote_relief_of (struct glyph_string *s);
static void remote_tool_bar_margin (int *x, int *y);
static Emacs_Rectangle remote_relief_area (struct glyph_string *s,
					   Emacs_Rectangle slice,
					   struct remote_relief relief);
static void remote_fill_sides (struct glyph_string *s, Emacs_Rectangle area,
			       int thick, unsigned long top_left,
			       unsigned long bottom_right,
			       Emacs_Rectangle clip);
static void remote_fill_clipped (struct frame *f, Emacs_Rectangle area,
				 Emacs_Rectangle clip, unsigned long color);
static struct remote_relief_colors remote_relief_colors (unsigned long);
static unsigned long remote_scale_color (unsigned long color, int numerator,
					 int denominator, int add);

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

/* Draw the relief of S as pgtk_draw_image_relief does.  The sides are
   rectangles: the top and left sides in one color, then the bottom and
   right sides in the other color.  A slice has only the sides that are
   at the edges of the image.  */
void
remote_draw_image_relief (struct glyph_string *s, Emacs_Rectangle slice,
			  Emacs_Rectangle clip)
{
  if (!s->img->relief
      && s->hl != DRAW_IMAGE_RAISED
      && s->hl != DRAW_IMAGE_SUNKEN)
    return;
  struct remote_relief relief = remote_relief_of (s);
  struct remote_relief_colors colors
    = remote_relief_colors (s->face->background);
  remote_fill_sides (s, remote_relief_area (s, slice, relief), relief.thick,
		     relief.raised ? colors.light : colors.dark,
		     relief.raised ? colors.dark : colors.light, clip);
}

/* Return the relief of S.  A highlight of a tool-bar button has the
   width `tool-bar-button-relief'.  Another relief has the width and
   the type of the `:relief' of the image.  The relief of a tool-bar
   button is around its margin `tool-bar-button-margin'.  */
static struct remote_relief
remote_relief_of (struct glyph_string *s)
{
  struct remote_relief relief = { 0 };
  if (s->hl == DRAW_IMAGE_RAISED || s->hl == DRAW_IMAGE_SUNKEN)
    {
      relief.thick = (tool_bar_button_relief < 0
		      ? DEFAULT_TOOL_BAR_BUTTON_RELIEF
		      : min (tool_bar_button_relief, 1000000));
      relief.raised = s->hl == DRAW_IMAGE_RAISED;
    }
  else
    {
      relief.thick = eabs (s->img->relief);
      relief.raised = s->img->relief > 0;
    }
  if (s->face->id == TOOL_BAR_FACE_ID)
    remote_tool_bar_margin (&relief.extra_x, &relief.extra_y);
  return relief;
}

/* Store the horizontal and the vertical `tool-bar-button-margin' in X
   and Y.  A value that is not a number or a pair of numbers is no
   margin.  */
static void
remote_tool_bar_margin (int *x, int *y)
{
  Lisp_Object margin = Vtool_bar_button_margin;
  if (CONSP (margin) && FIXNUMP (XCAR (margin)) && FIXNUMP (XCDR (margin)))
    {
      *x = XFIXNUM (XCAR (margin));
      *y = XFIXNUM (XCDR (margin));
    }
  else if (FIXNUMP (margin))
    *x = *y = XFIXNUM (margin);
}

/* Return the rectangle of the outer edges of the relief of the SLICE
   of S.  Each edge of the slice that is an edge of the image moves out
   by the width of RELIEF and its space.  */
static Emacs_Rectangle
remote_relief_area (struct glyph_string *s, Emacs_Rectangle slice,
		    struct remote_relief relief)
{
  int dx = relief.thick + relief.extra_x;
  int dy = relief.thick + relief.extra_y;
  int left = slice.x - (s->slice.x == 0 ? dx : 0);
  int top = slice.y - (s->slice.y == 0 ? dy : 0);
  int right = (slice.x + slice.width
	       + (s->slice.x + s->slice.width == s->img->width ? dx : 0));
  int bottom = (slice.y + slice.height
		+ (s->slice.y + s->slice.height == s->img->height ? dy : 0));
  return (Emacs_Rectangle) { left, top, right - left, bottom - top };
}

/* Fill the sides of AREA that are edges of the image of S.  The sides
   are THICK pixels wide.  The top and left sides have the color
   TOP_LEFT, and the bottom and right sides have the color
   BOTTOM_RIGHT.  Change only the pixels in CLIP.  */
static void
remote_fill_sides (struct glyph_string *s, Emacs_Rectangle area, int thick,
		   unsigned long top_left, unsigned long bottom_right,
		   Emacs_Rectangle clip)
{
  int right = area.x + area.width - thick;
  int bottom = area.y + area.height - thick;
  struct frame *f = s->f;
  if (s->slice.y == 0)
    remote_fill_clipped (f, (Emacs_Rectangle) { area.x, area.y, area.width,
						thick },
			 clip, top_left);
  if (s->slice.x == 0)
    remote_fill_clipped (f, (Emacs_Rectangle) { area.x, area.y, thick,
						area.height },
			 clip, top_left);
  if (s->slice.y + s->slice.height == s->img->height)
    remote_fill_clipped (f, (Emacs_Rectangle) { area.x, bottom, area.width,
						thick },
			 clip, bottom_right);
  if (s->slice.x + s->slice.width == s->img->width)
    remote_fill_clipped (f, (Emacs_Rectangle) { right, area.y, thick,
						area.height },
			 clip, bottom_right);
}

/* Fill the part of AREA of F inside CLIP with COLOR.  */
static void
remote_fill_clipped (struct frame *f, Emacs_Rectangle area,
		     Emacs_Rectangle clip, unsigned long color)
{
  Emacs_Rectangle part = remote_intersection (area, clip);
  remote_fill_area (f, part.x, part.y, part.width, part.height, color);
}

/* Return the light and the dark color of a relief on BACKGROUND, a
   color #xRRGGBB.  This is a simple form of
   pgtk_compute_lighter_color.  Each component of the light color is
   1.2 times that of BACKGROUND plus #x20, so a black background also
   gives a light color.  Each component of the dark color is 0.6 times
   that of BACKGROUND.  */
static struct remote_relief_colors
remote_relief_colors (unsigned long background)
{
  return (struct remote_relief_colors)
    {
      .light = remote_scale_color (background, 6, 5, 0x20),
      .dark = remote_scale_color (background, 3, 5, 0),
    };
}

/* Return COLOR, a color #xRRGGBB, with each component multiplied by
   NUMERATOR / DENOMINATOR and increased by ADD.  A component stays at
   most #xff.  */
static unsigned long
remote_scale_color (unsigned long color, int numerator, int denominator,
		    int add)
{
  unsigned long result = 0;
  for (int shift = 16; shift >= 0; shift -= 8)
    {
      unsigned long component = (color >> shift) & 0xff;
      unsigned long scaled = component * numerator / denominator + add;
      result |= min (scaled, 0xff) << shift;
    }
  return result;
}
