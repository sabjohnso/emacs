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
#include "remotegeom.h"
#include "remotetoolbar.h"

static int remote_tool_bar_lines_of (int height, int unit);
static void remote_store_tool_bar_height (struct frame *f, int height);
static void remote_resize_for_tool_bar (struct frame *f);
static void remote_rebuild_glyphs (struct frame *f);
static bool remote_tool_bar_takes_p (struct frame *f, int x, int y,
				     bool press);

/* The space between an image and its relief.  */
struct remote_margin
{
  int x, y;
};

/* The relief of an image: the width of the sides, the type, and the
   space between the image and the sides.  */
struct remote_relief
{
  int thick;
  bool raised;
  struct remote_margin extra;
};

/* The edges of a slice that are also edges of its image.  Only these
   edges get a side of the relief.  */
struct remote_edges
{
  bool top, left, bottom, right;
};

/* The two colors of a relief.  */
struct remote_relief_colors
{
  unsigned long light, dark;
};

/* One side of a relief: the rectangle, its color, and whether the
   relief has the side.  */
struct remote_side
{
  bool present;
  Emacs_Rectangle area;
  unsigned long color;
};

static struct remote_relief remote_relief_of (struct glyph_string *s);
static struct remote_relief remote_compute_relief (enum draw_glyphs_face hl,
						   int image_relief,
						   EMACS_INT button_relief,
						   bool tool_bar_p,
						   struct remote_margin);
static int remote_button_relief_width (EMACS_INT relief);
static struct remote_margin remote_tool_bar_margin (Lisp_Object margin,
						    EMACS_INT button_relief);
static int remote_margin_part (Lisp_Object part, int relief);
static int remote_layout_relief (EMACS_INT relief);
static struct remote_edges remote_slice_edges (struct glyph_string *s);
static Emacs_Rectangle remote_relief_area (Emacs_Rectangle slice,
					   struct remote_edges edges,
					   struct remote_relief relief);
static void remote_fill_sides (struct frame *f, Emacs_Rectangle area,
			       struct remote_edges edges, int thick,
			       unsigned long top_left,
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
  int lines = RANGED_FIXNUMP (0, value, INT_MAX) ? XFIXNAT (value) : 0;
  remote_change_tool_bar_height (f, lines * FRAME_LINE_HEIGHT (f));
}

/* Set the height of the tool bar of F to HEIGHT pixels.  The order of
   the steps is important.  First, store the height and the number of
   lines in F, because the change of the size reads them.  Then change
   the size of F and of its windows.  Last, make the glyph matrices
   for the new sizes, and mark F for a full redisplay.  */
void
remote_change_tool_bar_height (struct frame *f, int height)
{
  remote_store_tool_bar_height (f, height);
  remote_resize_for_tool_bar (f);
  remote_rebuild_glyphs (f);
}

/* Return the number of lines of UNIT pixels that hold HEIGHT
   pixels.  */
static int
remote_tool_bar_lines_of (int height, int unit)
{
  return (height + unit - 1) / unit;
}

/* Store HEIGHT as the tool bar height of F, and the number of lines
   in the `tool-bar-lines' parameter.  Mark F for redisplay.  */
static void
remote_store_tool_bar_height (struct frame *f, int height)
{
  int lines = remote_tool_bar_lines_of (height, FRAME_LINE_HEIGHT (f));
  fset_redisplay (f);
  FRAME_TOOL_BAR_HEIGHT (f) = height;
  FRAME_TOOL_BAR_LINES (f) = lines;
  store_frame_param (f, Qtool_bar_lines, make_fixnum (lines));
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

/* Make the glyph matrices of F for the sizes of its windows, and mark
   F for a full redisplay.  */
static void
remote_rebuild_glyphs (struct frame *f)
{
  adjust_frame_glyphs (f);
  SET_FRAME_GARBAGED (f);
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
  struct remote_edges edges = remote_slice_edges (s);
  struct remote_relief_colors colors
    = remote_relief_colors (s->face->background);
  remote_fill_sides (s->f, remote_relief_area (slice, edges, relief), edges,
		     relief.thick,
		     relief.raised ? colors.light : colors.dark,
		     relief.raised ? colors.dark : colors.light, clip);
}

/* Return the relief of S.  Read the Lisp variables of the relief of a
   tool-bar button here, and give their values to
   remote_compute_relief.  */
static struct remote_relief
remote_relief_of (struct glyph_string *s)
{
  struct remote_margin margin
    = remote_tool_bar_margin (Vtool_bar_button_margin,
			      tool_bar_button_relief);
  bool tool_bar_p = s->face->id == TOOL_BAR_FACE_ID;
  return remote_compute_relief (s->hl, s->img->relief,
				tool_bar_button_relief, tool_bar_p, margin);
}

/* Return the relief of an image with the highlight HL and the
   `:relief' IMAGE_RELIEF.  The raised or the sunken highlight of a
   tool-bar button has the width BUTTON_RELIEF.  Another relief has
   the width and the type of IMAGE_RELIEF.  If TOOL_BAR_P, the image
   is a tool-bar button, and the relief is around its MARGIN.  */
static struct remote_relief
remote_compute_relief (enum draw_glyphs_face hl, int image_relief,
		       EMACS_INT button_relief, bool tool_bar_p,
		       struct remote_margin margin)
{
  bool highlight = hl == DRAW_IMAGE_RAISED || hl == DRAW_IMAGE_SUNKEN;
  return (struct remote_relief)
    {
      .thick = (highlight ? remote_button_relief_width (button_relief)
		: eabs (image_relief)),
      .raised = highlight ? hl == DRAW_IMAGE_RAISED : image_relief > 0,
      .extra = tool_bar_p ? margin : (struct remote_margin) { 0, 0 },
    };
}

/* Return the width of the relief of a tool-bar button for the value
   RELIEF of `tool-bar-button-relief'.  A negative value gives the
   default width.  As in pgtk_draw_image_relief, the width is at most
   1000000, so that the sums of the relief area stay in an int.  */
static int
remote_button_relief_width (EMACS_INT relief)
{
  return min (remote_layout_relief (relief), 1000000);
}

/* Return the relief that the layout in xdisp.c gives to a tool-bar
   button for the value RELIEF of `tool-bar-button-relief'.  */
static int
remote_layout_relief (EMACS_INT relief)
{
  return (relief < 0 ? DEFAULT_TOOL_BAR_BUTTON_RELIEF
	  : min (relief, min (INT_MAX, MOST_POSITIVE_FIXNUM)));
}

/* Return the horizontal and the vertical space of the value MARGIN of
   `tool-bar-button-margin', with the value BUTTON_RELIEF of
   `tool-bar-button-relief'.  A number gives both spaces, and a pair
   gives one space with each part.  The rule of a part is the rule of
   the layout in xdisp.c, so the relief is around the space that the
   layout gives.  */
static struct remote_margin
remote_tool_bar_margin (Lisp_Object margin, EMACS_INT button_relief)
{
  int relief = remote_layout_relief (button_relief);
  return (CONSP (margin)
	  ? (struct remote_margin) {
	      remote_margin_part (XCAR (margin), relief),
	      remote_margin_part (XCDR (margin), relief) }
	  : (struct remote_margin) {
	      remote_margin_part (margin, relief),
	      remote_margin_part (margin, relief) });
}

/* Return the space of PART of a margin, with the relief RELIEF of the
   layout.  As in xdisp.c, a part that is not an integer from 1 to
   INT_MAX - RELIEF gives no space.  As the width of a relief, the
   space is at most 1000000, so that the sums of the relief area stay
   in an int.  */
static int
remote_margin_part (Lisp_Object part, int relief)
{
  return (RANGED_FIXNUMP (1, part, INT_MAX - relief)
	  ? min (XFIXNAT (part), 1000000) : 0);
}

/* Return the edges of the slice of S that are edges of its image.  */
static struct remote_edges
remote_slice_edges (struct glyph_string *s)
{
  return (struct remote_edges)
    {
      .top = s->slice.y == 0,
      .left = s->slice.x == 0,
      .bottom = s->slice.y + s->slice.height == s->img->height,
      .right = s->slice.x + s->slice.width == s->img->width,
    };
}

/* Return the rectangle of the outer edges of the relief of SLICE.
   Each of the EDGES moves out by the width of RELIEF and its space.  */
static Emacs_Rectangle
remote_relief_area (Emacs_Rectangle slice, struct remote_edges edges,
		    struct remote_relief relief)
{
  int dx = relief.thick + relief.extra.x;
  int dy = relief.thick + relief.extra.y;
  int left = slice.x - (edges.left ? dx : 0);
  int top = slice.y - (edges.top ? dy : 0);
  int right = slice.x + slice.width + (edges.right ? dx : 0);
  int bottom = slice.y + slice.height + (edges.bottom ? dy : 0);
  return (Emacs_Rectangle) { left, top, right - left, bottom - top };
}

/* Fill the sides of AREA of F that are EDGES.  The sides are THICK
   pixels wide.  The top and left sides have the color TOP_LEFT, and
   the bottom and right sides have the color BOTTOM_RIGHT.  Change only
   the pixels in CLIP.  */
static void
remote_fill_sides (struct frame *f, Emacs_Rectangle area,
		   struct remote_edges edges, int thick,
		   unsigned long top_left, unsigned long bottom_right,
		   Emacs_Rectangle clip)
{
  int right = area.x + area.width - thick;
  int bottom = area.y + area.height - thick;
  struct remote_side sides[] = {
    { edges.top, { area.x, area.y, area.width, thick }, top_left },
    { edges.left, { area.x, area.y, thick, area.height }, top_left },
    { edges.bottom, { area.x, bottom, area.width, thick }, bottom_right },
    { edges.right, { right, area.y, thick, area.height }, bottom_right },
  };
  for (int i = 0; i < countof (sides); i++)
    if (sides[i].present)
      remote_fill_clipped (f, sides[i].area, clip, sides[i].color);
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
