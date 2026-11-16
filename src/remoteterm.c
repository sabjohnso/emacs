/* The terminal of the remote display backend.

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

/* A remote terminal is a session.  `x-open-connection' creates a
   session, and `x-create-frame' creates frames on it.  The session
   and its frames exist while no client is connected.  The drawing
   hooks do nothing until a client can receive their output.  */

#include <config.h>

#include "lisp.h"
#include "blockinput.h"
#include "dispextern.h"
#include "font.h"
#include "fontset.h"
#include "frame.h"
#include "keyboard.h"
#include "termhooks.h"
#include "window.h"
#include "remoteterm.h"
#include "remoteinput.h"
#include "remotescroll.h"
#include "rdproto.h"

#include <fcntl.h>
#include <math.h>
#include "sha256.h"

struct remote_display_info *x_display_list;

/* The name_list_element of each session.  The garbage collector
   reaches those conses through this list.  */
static Lisp_Object remote_display_name_list;

/* The color names of etc/rgb.txt, as (NAME . PIXEL) pairs.  */
static Lisp_Object remote_color_map;

/* The fonts of all sessions.  A font has one ID for the life of the
   server process, so each session and each attach of a client use the same
   ID.  REMOTE_FONTS maps a font object to its entry, and it is weak,
   so it does not keep a closed font.  REMOTE_FONT_IDENTITIES maps the
   identity (HASH INDEX SIZE) of a font to its entry, so two font
   objects of the same font have one ID.  An entry is the vector
   [ID HASH INDEX SIZE FILE-SIZE FILE].  HASH is a unibyte string,
   FILE-SIZE is the number of bytes of the file, and FILE is the name
   of the file.  */
static Lisp_Object remote_fonts;
static Lisp_Object remote_font_identities;
static uint32_t remote_last_font_id;

/* The two colors of a draw: the color of the glyphs, and the color of
   the area under them.  */
struct remote_colors
{
  unsigned long foreground;
  unsigned long background;
};

static int remote_largest_frame_size (Lisp_Object, bool);
static int remote_millimeters (int, double);
static struct frame *decode_remote_frame (Lisp_Object);
static Lisp_Object remote_frame_edges (struct frame *, Lisp_Object);
static Lisp_Object remote_frame_geometry (struct frame *);
static Lisp_Object remote_size_pair (int, int);
static struct remote_display_info *remote_open_session (Lisp_Object);
static struct remote_display_info *remote_session_by_name (Lisp_Object);
static struct terminal *remote_create_terminal (struct remote_display_info *,
						Lisp_Object);
static Lisp_Object remote_create_frame (Lisp_Object);
static bool remote_terminal_has_frames (struct terminal *);
static Lisp_Object remote_frame_session_arg (Lisp_Object);
static struct frame *remote_make_frame_on (struct remote_display_info *,
					   Lisp_Object);
static struct frame *
remote_make_frame_with_minibuffer (struct remote_display_info *,
				   Lisp_Object);
static Lisp_Object remote_parent_frame_arg (struct remote_display_info *,
					    Lisp_Object);
static bool remote_session_frame_p (Lisp_Object,
				    struct remote_display_info *);
static void remote_set_initial_parent (struct frame *, Lisp_Object);
static bool remote_initially_visible_p (Lisp_Object);
static void remote_assign_frame_id (struct frame *);
static bool remote_frame_id_used_p (struct remote_display_info *, uint32_t);
static uint32_t remote_frame_id (struct frame *);
static void remote_init_frame_font (struct frame *, Lisp_Object);
static void remote_default_frame_parameters (struct frame *, Lisp_Object);
static void remote_finish_frame (struct frame *, Lisp_Object);
static void remote_update_scroll_bar_width (struct frame *);
static void remote_set_scroll_bar_default_width (struct frame *);
static void remote_forget_mouse_frame (struct remote_display_info *,
				       struct frame *);
static void remote_free_frame_resources (struct frame *);
static void remote_frame_rehighlight_hook (struct frame *);
static void remote_frame_rehighlight (struct remote_display_info *);
static struct frame *remote_input_frame (struct remote_display_info *,
					 struct frame *);
static void remote_redraw_cursors (struct frame *);
static void remote_delete_terminal (struct terminal *);
static bool remote_defined_color (struct frame *, const char *,
				  Emacs_Color *, bool, bool);
static void remote_set_cursor_type (struct frame *, Lisp_Object, Lisp_Object);
static struct redisplay_interface remote_redisplay_interface;
static void remote_send_draw (struct frame *, const struct rd_message *);
static void remote_define_frame (struct frame *);
static void remote_forget_frames (struct remote_display_info *);
static void remote_send_delete_frame (struct frame *);
static void remote_update_end (struct frame *);
static void remote_flush_frame (struct frame *);
static void remote_frame_up_to_date (struct frame *);
static void remote_clear_glyph_string (struct glyph_string *);
static Emacs_Rectangle remote_glyph_clip (struct glyph_string *);
static Emacs_Rectangle remote_intersection (Emacs_Rectangle, Emacs_Rectangle);
static int remote_foreground_x (struct glyph_string *);
static void remote_draw_chars (struct glyph_string *);
static void remote_draw_image (struct glyph_string *);
static Emacs_Rectangle remote_image_slice_area (struct glyph_string *);
static void remote_send_image (struct glyph_string *, uint32_t,
			       Emacs_Rectangle);
static void remote_draw_outline (struct frame *, Emacs_Rectangle,
				 unsigned long);
static void remote_draw_composition (struct glyph_string *);
static void remote_draw_static_composition (struct glyph_string *);
static void remote_draw_automatic_composition (struct glyph_string *);
static void remote_send_glyphs (struct glyph_string *, int, int, int, int);
static void remote_clear_frame_area (struct frame *, int, int, int, int);
static uint32_t remote_font_id (Lisp_Object);
static bool remote_entry_has_hash (Lisp_Object, const unsigned char *);
static Lisp_Object remote_font_entry (struct font *);
static Lisp_Object remote_font_hash (struct font *, uint32_t *);
static Lisp_Object remote_font_identity (struct font *, Lisp_Object,
					 Lisp_Object, Lisp_Object);
static void remote_ensure_font_defined (struct remote_display_info *,
					Lisp_Object);
static bool remote_font_defined_p (struct remote_display_info *, uint32_t);
static void remote_define_font (struct remote_display_info *, Lisp_Object);
static struct remote_colors remote_glyph_string_colors (struct glyph_string *);
static struct remote_colors remote_cursor_colors (struct frame *,
						  struct face *);
static unsigned long remote_cursor_color (struct frame *, struct glyph *,
					  enum text_cursor_kinds);
static unsigned long
remote_fringe_foreground (struct frame *,
			  const struct draw_fringe_bitmap_params *);
static bool remote_send_cursor (struct window *, struct glyph_row *,
				enum text_cursor_kinds, int);
static int remote_clip_copy (int, int, int *, int *, int);
static bool remote_clip_to_row (struct window *, struct glyph_row *,
				Emacs_Rectangle *);
static enum rd_cursor_kind remote_cursor_kind (enum text_cursor_kinds);
static bool remote_empty_area_p (int, int);
static void remote_row_bounds (struct window *, struct glyph_row *,
			       int *, int *);
static void remote_fringe_visible_rows (struct window *, struct glyph_row *,
					const struct draw_fringe_bitmap_params *,
					int *, int *);
static bool remote_fringe_fits_p (const struct draw_fringe_bitmap_params *,
				  int, int);
static int remote_fringe_rows_above (const struct draw_fringe_bitmap_params *);


/***********************************************************************
			       Sessions
 ***********************************************************************/

DEFUN ("x-open-connection", Fx_open_connection, Sx_open_connection,
       1, 3, 0,
       doc: /* Open a remote display session with the name DISPLAY.
The session exists while no client is connected to it.  XRM-STRING
and MUST-SUCCEED are ignored.  */)
  (Lisp_Object display, Lisp_Object xrm_string, Lisp_Object must_succeed)
{
  CHECK_STRING (display);
  if (!remote_session_by_name (display))
    remote_open_session (display);
  return Qnil;
}

DEFUN ("x-close-connection", Fx_close_connection, Sx_close_connection,
       1, 1, 0,
       doc: /* Close the remote display session TERMINAL.
TERMINAL is a terminal, a frame, or the name of a session.  */)
  (Lisp_Object terminal)
{
  if (STRINGP (terminal) && !remote_session_by_name (terminal))
    error ("No remote display session is named %s", SSDATA (terminal));
  struct remote_display_info *dpyinfo = check_x_display_info (terminal);
  if (remote_terminal_has_frames (dpyinfo->terminal))
    error ("Session still has frames on it");
  /* Fdelete_terminal runs `delete-terminal-functions', so that a
     listener of the session stops, and then remote_delete_terminal.  */
  Lisp_Object object;
  XSETTERMINAL (object, dpyinfo->terminal);
  Fdelete_terminal (object, Qt);
  return Qnil;
}

DEFUN ("x-display-list", Fx_display_list, Sx_display_list, 0, 0, 0,
       doc: /* Return the list of names of the remote display sessions.  */)
  (void)
{
  Lisp_Object result = Qnil;
  for (struct remote_display_info *d = x_display_list; d; d = d->next)
    result = Fcons (XCAR (d->name_list_element), result);
  return result;
}

DEFUN ("x-create-frame", Fx_create_frame, Sx_create_frame, 1, 1, 0,
       doc: /* Create a frame on a remote display session.
PARMS is an alist of frame parameters.  The `display' parameter names
the session.  */)
  (Lisp_Object parms)
{
  return remote_create_frame (parms);
}

DEFUN ("xw-display-color-p", Fxw_display_color_p, Sxw_display_color_p,
       0, 1, 0,
       doc: /* Return t, because a remote display shows colors.
TERMINAL is ignored.  */)
  (Lisp_Object terminal)
{
  return Qt;
}

DEFUN ("x-display-grayscale-p", Fx_display_grayscale_p,
       Sx_display_grayscale_p, 0, 1, 0,
       doc: /* Return t, because a remote display shows shades of gray.
TERMINAL is ignored.  */)
  (Lisp_Object terminal)
{
  return Qt;
}

DEFUN ("xw-color-defined-p", Fxw_color_defined_p, Sxw_color_defined_p,
       1, 2, 0,
       doc: /* Return t if COLOR names a color.  FRAME is ignored.  */)
  (Lisp_Object color, Lisp_Object frame)
{
  Emacs_Color c;
  CHECK_STRING (color);
  return remote_defined_color (NULL, SSDATA (color), &c, false, false)
    ? Qt : Qnil;
}

DEFUN ("xw-color-values", Fxw_color_values, Sxw_color_values, 1, 2, 0,
       doc: /* Return the RGB values of COLOR as a list of three integers.
Each value is from 0 to 65535.  Return nil if COLOR is not a color.
FRAME is ignored.  */)
  (Lisp_Object color, Lisp_Object frame)
{
  Emacs_Color c;
  CHECK_STRING (color);
  if (!remote_defined_color (NULL, SSDATA (color), &c, false, false))
    return Qnil;
  return list3i (c.red, c.green, c.blue);
}

DEFUN ("x-display-color-cells", Fx_display_color_cells,
       Sx_display_color_cells, 0, 1, 0,
       doc: /* Return the number of colors that a remote display shows.
TERMINAL is ignored.  */)
  (Lisp_Object terminal)
{
  return make_fixnum (1 << 24);
}

DEFUN ("x-display-pixel-width", Fx_display_pixel_width,
       Sx_display_pixel_width, 0, 1, 0,
       doc: /* Return the width in pixels of the session TERMINAL.
A session has no screen of its own, so the value is the width of the
widest frame of the session.  */)
  (Lisp_Object terminal)
{
  return make_fixnum (remote_largest_frame_size (terminal, true));
}

DEFUN ("x-display-pixel-height", Fx_display_pixel_height,
       Sx_display_pixel_height, 0, 1, 0,
       doc: /* Return the height in pixels of the session TERMINAL.
A session has no screen of its own, so the value is the height of the
tallest frame of the session.  */)
  (Lisp_Object terminal)
{
  return make_fixnum (remote_largest_frame_size (terminal, false));
}

DEFUN ("x-display-mm-width", Fx_display_mm_width, Sx_display_mm_width,
       0, 1, 0,
       doc: /* Return the width in millimeters of the session TERMINAL.
The value is the width in pixels at the resolution of the session.  */)
  (Lisp_Object terminal)
{
  struct remote_display_info *dpyinfo = check_x_display_info (terminal);
  return make_fixnum (remote_millimeters (remote_largest_frame_size
					  (terminal, true),
					  dpyinfo->resx));
}

DEFUN ("x-display-mm-height", Fx_display_mm_height, Sx_display_mm_height,
       0, 1, 0,
       doc: /* Return the height in millimeters of the session TERMINAL.
The value is the height in pixels at the resolution of the session.  */)
  (Lisp_Object terminal)
{
  struct remote_display_info *dpyinfo = check_x_display_info (terminal);
  return make_fixnum (remote_millimeters (remote_largest_frame_size
					  (terminal, false),
					  dpyinfo->resy));
}

DEFUN ("x-display-planes", Fx_display_planes, Sx_display_planes, 0, 1, 0,
       doc: /* Return the number of bit planes of the session TERMINAL.  */)
  (Lisp_Object terminal)
{
  return make_fixnum (check_x_display_info (terminal)->n_planes);
}

DEFUN ("x-display-visual-class", Fx_display_visual_class,
       Sx_display_visual_class, 0, 1, 0,
       doc: /* Return `true-color', because a remote display shows each
color that it gets.  TERMINAL is ignored.  */)
  (Lisp_Object terminal)
{
  return Qtrue_color;
}

DEFUN ("remote-frame-geometry", Fremote_frame_geometry,
       Sremote_frame_geometry, 0, 1, 0,
       doc: /* Return the geometric attributes of the remote frame FRAME.
See also `frame-geometry'.  */)
  (Lisp_Object frame)
{
  return remote_frame_geometry (decode_remote_frame (frame));
}

DEFUN ("remote-frame-edges", Fremote_frame_edges, Sremote_frame_edges,
       0, 2, 0,
       doc: /* Return the edges of the remote frame FRAME.
TYPE is as for `frame-edges'.  */)
  (Lisp_Object frame, Lisp_Object type)
{
  return remote_frame_edges (decode_remote_frame (frame), type);
}

DEFUN ("x-hide-tip", Fx_hide_tip, Sx_hide_tip, 0, 0, 0,
       doc: /* Hide the current tooltip window.
Return t if a tooltip was hidden, and nil otherwise.  The Lisp
function `remote-display--hide-tip' does the work.  This function is
in C because menu.c calls it.  */)
  (void)
{
  return call0 (Qremote_display__hide_tip);
}

/* Return the largest pixel width of the frames of the session TERMINAL
   if WIDTH_P, and otherwise their largest pixel height.  */
static int
remote_largest_frame_size (Lisp_Object terminal, bool width_p)
{
  struct remote_display_info *dpyinfo = check_x_display_info (terminal);
  int size = 0;
  Lisp_Object tail, frame;
  FOR_EACH_FRAME (tail, frame)
    {
      struct frame *f = XFRAME (frame);
      if (FRAME_REMOTE_P (f) && FRAME_DISPLAY_INFO (f) == dpyinfo)
	size = max (size, (width_p ? FRAME_PIXEL_WIDTH (f)
			   : FRAME_PIXEL_HEIGHT (f)));
    }
  return size;
}

/* Return the number of millimeters of PIXELS at the resolution
   DOTS_PER_INCH, rounded to the nearest millimeter.  */
static int
remote_millimeters (int pixels, double dots_per_inch)
{
  eassert (dots_per_inch > 0);
  return lround (pixels * 25.4 / dots_per_inch);
}

/* Return the live remote frame of FRAME, or the selected frame if
   FRAME is nil.  Signal an error for any other frame.  */
static struct frame *
decode_remote_frame (Lisp_Object frame)
{
  struct frame *f = decode_live_frame (frame);
  if (!FRAME_REMOTE_P (f))
    error ("Not a remote frame");
  return f;
}

/* Return the edges of F of the type TYPE: Qouter_edges, Qinner_edges,
   or any other value for the native edges.  The session does not know
   the decorations that the client adds to a frame, so the outer edges
   are the native edges.  The bars are inside the frame, as the window
   layout puts them.  */
static Lisp_Object
remote_frame_edges (struct frame *f, Lisp_Object type)
{
  int left = f->left_pos;
  int top = f->top_pos;
  int right = left + FRAME_PIXEL_WIDTH (f);
  int bottom = top + FRAME_PIXEL_HEIGHT (f);
  int border = FRAME_INTERNAL_BORDER_WIDTH (f);

  if (EQ (type, Qinner_edges))
    return list4i (left + border,
		   top + border + FRAME_TOP_MARGIN_HEIGHT (f),
		   right - border,
		   bottom - border - FRAME_TOOL_BAR_BOTTOM_HEIGHT (f));
  return list4i (left, top, right, bottom);
}

/* Return the geometry of F as `frame-geometry' gives it.  F has no
   decorations, no external bars, and no outer border.  */
static Lisp_Object
remote_frame_geometry (struct frame *f)
{
  int width = FRAME_PIXEL_WIDTH (f);
  int bar_width = width - 2 * FRAME_INTERNAL_BORDER_WIDTH (f);
  int menu_bar_height = FRAME_MENU_BAR_HEIGHT (f);
  int tab_bar_height = FRAME_TAB_BAR_HEIGHT (f);
  int tool_bar_height = FRAME_TOOL_BAR_HEIGHT (f);
  return list (Fcons (Qouter_position,
		      remote_size_pair (f->left_pos, f->top_pos)),
	       Fcons (Qouter_size,
		      remote_size_pair (width, FRAME_PIXEL_HEIGHT (f))),
	       Fcons (Qouter_border_width, make_fixnum (0)),
	       Fcons (Qexternal_border_size, remote_size_pair (0, 0)),
	       Fcons (Qtitle_bar_size, remote_size_pair (0, 0)),
	       Fcons (Qmenu_bar_external, Qnil),
	       Fcons (Qmenu_bar_size,
		      remote_size_pair (menu_bar_height ? width : 0,
					menu_bar_height)),
	       Fcons (Qtab_bar_size,
		      remote_size_pair (tab_bar_height ? bar_width : 0,
					tab_bar_height)),
	       Fcons (Qtool_bar_external, Qnil),
	       Fcons (Qtool_bar_position, FRAME_TOOL_BAR_POSITION (f)),
	       Fcons (Qtool_bar_size,
		      remote_size_pair (tool_bar_height ? bar_width : 0,
					tool_bar_height)),
	       Fcons (Qinternal_border_width,
		      make_fixnum (FRAME_INTERNAL_BORDER_WIDTH (f))));
}

/* Return the cons (A . B) of two fixnums.  */
static Lisp_Object
remote_size_pair (int a, int b)
{
  return Fcons (make_fixnum (a), make_fixnum (b));
}

/* Return the session for OBJECT: a terminal, a frame, the name of a
   session, or nil for the session of the selected frame.  */
struct remote_display_info *
check_x_display_info (Lisp_Object object)
{
  if (NILP (object))
    {
      struct frame *sf = XFRAME (selected_frame);
      if (FRAME_REMOTE_P (sf) && FRAME_LIVE_P (sf))
	return FRAME_DISPLAY_INFO (sf);
      if (x_display_list)
	return x_display_list;
      error ("No remote display session is open");
    }
  if (STRINGP (object))
    {
      /* As for X, a frame on a new display opens a session.  */
      struct remote_display_info *dpyinfo = remote_session_by_name (object);
      return dpyinfo ? dpyinfo : remote_open_session (object);
    }
  struct terminal *t = decode_live_terminal (object);
  if (t->type != output_remote)
    error ("Terminal %d is not a remote display session", t->id);
  return t->display_info.remote;
}

static struct remote_display_info *
remote_session_by_name (Lisp_Object name)
{
  for (struct remote_display_info *d = x_display_list; d; d = d->next)
    if (!NILP (Fstring_equal (XCAR (d->name_list_element), name)))
      return d;
  return NULL;
}

static struct remote_display_info *
remote_open_session (Lisp_Object name)
{
  struct remote_display_info *dpyinfo = xzalloc (sizeof *dpyinfo);
  remote_init_images (dpyinfo);
  dpyinfo->name_list_element = Fcons (name, Qnil);
  remote_display_name_list = Fcons (dpyinfo->name_list_element,
				    remote_display_name_list);
  dpyinfo->resx = dpyinfo->resy = 96;
  dpyinfo->n_planes = 24;
  dpyinfo->smallest_char_width = 1;
  dpyinfo->smallest_font_height = 1;
  reset_mouse_highlight (&dpyinfo->mouse_highlight);

  if (!x_display_list)
    gui_init_fringe (&remote_redisplay_interface);
  dpyinfo->next = x_display_list;
  x_display_list = dpyinfo;

  remote_create_terminal (dpyinfo, name);
  return dpyinfo;
}

static bool
remote_terminal_has_frames (struct terminal *terminal)
{
  Lisp_Object tail, frame;
  FOR_EACH_FRAME (tail, frame)
    if (FRAME_TERMINAL (XFRAME (frame)) == terminal)
      return true;
  return false;
}

/* The delete_terminal_hook.  Delete TERMINAL and its session.  */
static void
remote_delete_terminal (struct terminal *terminal)
{
  struct remote_display_info *dpyinfo = terminal->display_info.remote;
  if (!terminal->name)
    return;

  block_input ();
  delete_terminal (terminal);
  if (x_display_list == dpyinfo)
    x_display_list = dpyinfo->next;
  else
    for (struct remote_display_info *d = x_display_list; d; d = d->next)
      if (d->next == dpyinfo)
	{
	  d->next = dpyinfo->next;
	  break;
	}
  remote_display_name_list = Fdelq (dpyinfo->name_list_element,
				    remote_display_name_list);
  remote_set_sink (dpyinfo, NULL);
  remote_free_images (dpyinfo);
  xfree (dpyinfo->font_defined);
  xfree (dpyinfo->bitmaps);
  xfree (dpyinfo);
  terminal->display_info.remote = NULL;
  unblock_input ();
}


/***********************************************************************
				Output
 ***********************************************************************/

/* A sink of a session.  Only this file knows the contents.  */
struct remote_sink
{
  const struct remote_sink_ops *ops;
  void *data;
};

/* Send MESSAGE to the client of DPYINFO.  Do nothing if no client is
   attached.  A draw on a frame must go through remote_send_draw
   instead, so the client knows the frame first.  */
void
remote_send_message (struct remote_display_info *dpyinfo,
		     const struct rd_message *message)
{
  struct remote_sink *sink = dpyinfo->sink;
  if (!sink)
    return;
  USE_SAFE_ALLOCA;
  ptrdiff_t size = rd_encoded_size (message);
  unsigned char *bytes = SAFE_ALLOCA (size);
  rd_encode (message, bytes);
  sink->ops->write (sink->data, bytes, size);
  SAFE_FREE ();
}

/* Send MESSAGE, a draw on the frame F, to the client of F.  First
   tell the client about F, if it does not know F or its size.  */
static void
remote_send_draw (struct frame *f, const struct rd_message *message)
{
  remote_define_frame (f);
  remote_send_message (FRAME_DISPLAY_INFO (f), message);
}

/* Send a define-frame message for F, if the client of F does not know
   F or its size.  With no client, the session sends nothing, and the
   next attach marks F as unknown again.  */
static void
remote_define_frame (struct frame *f)
{
  struct remote_output *output = FRAME_REMOTE_OUTPUT (f);
  int width = FRAME_PIXEL_WIDTH (f);
  int height = FRAME_PIXEL_HEIGHT (f);
  if (output->defined && output->defined_width == width
      && output->defined_height == height)
    return;
  if (!output->defined)
    remote_place_new_frame (f);
  struct rd_message message =
    {
      .type = RD_DEFINE_FRAME,
      .u.define_frame = { .frame = remote_frame_id (f),
			  .width = width, .height = height },
    };
  remote_send_message (FRAME_DISPLAY_INFO (f), &message);
  output->defined = true;
  output->defined_width = width;
  output->defined_height = height;
}

/* Send a delete-frame message for F, if the client of F knows F.  F
   is not usable after this call.  */
static void
remote_send_delete_frame (struct frame *f)
{
  if (!FRAME_REMOTE_OUTPUT (f)->defined)
    return;
  struct rd_message message =
    {
      .type = RD_DELETE_FRAME,
      .u.delete_frame = { .frame = remote_frame_id (f) },
    };
  remote_send_message (FRAME_DISPLAY_INFO (f), &message);
}

/* Make the client of DPYINFO forget its frames.  Mark each frame as
   unknown to the client, and as garbaged, so that the next redisplay
   defines and draws all of it.  */
static void
remote_forget_frames (struct remote_display_info *dpyinfo)
{
  Lisp_Object tail, frame;
  FOR_EACH_FRAME (tail, frame)
    {
      struct frame *f = XFRAME (frame);
      if (FRAME_REMOTE_P (f) && FRAME_DISPLAY_INFO (f) == dpyinfo)
	{
	  FRAME_REMOTE_OUTPUT (f)->defined = false;
	  remote_forget_place (f);
	  SET_FRAME_GARBAGED (f);
	}
    }
}

/* End the update of F.  This is the update_end_hook.  Let a motion
   change the mouse highlight again, as x_update_end does: the start of
   the update of a window of the mouse frame stops it
   (gui_update_window_begin).  Then send the messages of the update to
   the client.  */
static void
remote_update_end (struct frame *f)
{
  FRAME_DISPLAY_INFO (f)->mouse_highlight.mouse_face_defer = false;
  remote_flush_frame (f);
}

/* Tell the sink of F that the messages so far form a whole, and must
   go to the client.  remote_update_end calls this, and this is the
   flush_display hook of the redisplay interface.  Both are necessary:
   redisplay calls the second one only for the selected frame, and it
   calls it for messages outside an update.  First send the image IDs
   that the client must forget.  */
static void
remote_flush_frame (struct frame *f)
{
  remote_send_image_forgets (FRAME_DISPLAY_INFO (f));
  struct remote_sink *sink = FRAME_DISPLAY_INFO (f)->sink;
  if (sink)
    sink->ops->flush (sink->data);
}

/* The frame_up_to_date_hook.  The update of F ended, so send the image
   messages that the update did not send.  If the flush at the end of
   the update sent all the bytes, the transport does not call
   remote_output_drained, so continue here.  */
static void
remote_frame_up_to_date (struct frame *f)
{
  remote_send_images (FRAME_DISPLAY_INFO (f));
}

/* The transport of the client of DPYINFO calls this when it sent
   all the bytes that it had.  */
void
remote_output_drained (struct remote_display_info *dpyinfo)
{
  remote_send_font_data (dpyinfo);
  remote_send_clipboard_data (dpyinfo);
  remote_send_menu_messages (dpyinfo);
  remote_send_images (dpyinfo);
}

/* Call SEND_ONE with DPYINFO while the sink of DPYINFO has fewer than
   REMOTE_PACED_QUEUE_LIMIT bytes that it did not send.  SEND_ONE sends
   one message, and returns false if it has nothing to send.  Flush the
   sink each time: if the connection takes all the bytes, the transport
   does not call remote_output_drained, so continue here.  */
void
remote_send_paced (struct remote_display_info *dpyinfo,
		   bool (*send_one) (struct remote_display_info *))
{
  for (;;)
    {
      bool more = true;
      while (more && remote_queued (dpyinfo) < REMOTE_PACED_QUEUE_LIMIT)
	more = send_one (dpyinfo);
      remote_flush (dpyinfo);
      if (!more || remote_queued (dpyinfo) > 0)
	return;
    }
}

/* Return the number of bytes that the sink of DPYINFO did not send,
   or 0 if DPYINFO has no client.  */
ptrdiff_t
remote_queued (struct remote_display_info *dpyinfo)
{
  struct remote_sink *sink = dpyinfo->sink;
  return sink ? sink->ops->queued (sink->data) : 0;
}

/* Send the bytes that the sink of DPYINFO keeps, if DPYINFO has a
   client.  */
void
remote_flush (struct remote_display_info *dpyinfo)
{
  struct remote_sink *sink = dpyinfo->sink;
  if (sink)
    sink->ops->flush (sink->data);
}

/* Return a new sink with the operations OPS and the data DATA.  */
struct remote_sink *
remote_make_sink (const struct remote_sink_ops *ops, void *data)
{
  struct remote_sink *sink = xmalloc (sizeof *sink);
  sink->ops = ops;
  sink->data = data;
  return sink;
}

/* Return the data of the sink of DPYINFO if the sink has the operations
   OPS, and otherwise NULL.  */
void *
remote_sink_data (struct remote_display_info *dpyinfo,
		  const struct remote_sink_ops *ops)
{
  struct remote_sink *sink = dpyinfo->sink;
  return sink && sink->ops == ops ? sink->data : NULL;
}

/* Return true if a client is attached to the session of the remote
   frame F.  */
bool
remote_frame_has_client (struct frame *f)
{
  return FRAME_DISPLAY_INFO (f)->sink != NULL;
}

/* Free the sink of DPYINFO if it has one, and give DPYINFO the sink
   SINK.  If SINK is NULL, DPYINFO has no client after this call.  */
void
remote_set_sink (struct remote_display_info *dpyinfo, struct remote_sink *sink)
{
  struct remote_sink *old = dpyinfo->sink;
  dpyinfo->sink = sink;
  remote_drop_font_transfers (dpyinfo);
  remote_drop_clipboard (dpyinfo);
  remote_reset_images (dpyinfo);
  remote_drop_menu (dpyinfo);

  /* A new client has no font or frame definitions, and shows
     nothing.  */
  remote_forget_frames (dpyinfo);
  if (dpyinfo->font_defined)
    memset (dpyinfo->font_defined, 0, dpyinfo->font_defined_size);
  if (old)
    {
      old->ops->free (old->data);
      xfree (old);
    }
}

/***********************************************************************
				Colors
 ***********************************************************************/

/* Return the color names of etc/rgb.txt.  Load them on first use.  */
static Lisp_Object
color_map (void)
{
  if (NILP (remote_color_map))
    remote_color_map
      = Fx_load_color_file (Fexpand_file_name (build_string ("rgb.txt"),
					       Vdata_directory));
  return remote_color_map;
}

/* The defined_color_hook.  */
static bool
remote_defined_color (struct frame *f, const char *name,
		      Emacs_Color *color, bool alloc, bool make_index)
{
  unsigned short r, g, b;
  if (parse_color_spec (name, &r, &g, &b))
    {
      color->red = r;
      color->green = g;
      color->blue = b;
      color->pixel = RGB_TO_ULONG (r >> 8, g >> 8, b >> 8);
      return true;
    }

  Lisp_Object entry = Fassoc_string (build_string (name), color_map (), Qt);
  if (!CONSP (entry))
    return false;
  unsigned long pixel = XFIXNUM (XCDR (entry));
  color->pixel = pixel;
  color->red = RED_FROM_ULONG (pixel) * 257;
  color->green = GREEN_FROM_ULONG (pixel) * 257;
  color->blue = BLUE_FROM_ULONG (pixel) * 257;
  return true;
}

/* The query_colors hook.  Set the RGB values from the pixels.  */
static void
remote_query_colors (struct frame *f, Emacs_Color *colors, int ncolors)
{
  for (int i = 0; i < ncolors; i++)
    {
      unsigned long pixel = colors[i].pixel;
      colors[i].red = RED_FROM_ULONG (pixel) * 257;
      colors[i].green = GREEN_FROM_ULONG (pixel) * 257;
      colors[i].blue = BLUE_FROM_ULONG (pixel) * 257;
    }
}

static void
remote_query_frame_background_color (struct frame *f, Emacs_Color *bgcolor)
{
  bgcolor->pixel = FRAME_BACKGROUND_PIXEL (f);
  remote_query_colors (f, bgcolor, 1);
}

/* Return the pixel of the color ARG, or DEFAULT_PIXEL if ARG is not
   a color.  */
static unsigned long
remote_decode_color (struct frame *f, Lisp_Object arg,
		     unsigned long default_pixel)
{
  Emacs_Color color;
  if (STRINGP (arg) && remote_defined_color (f, SSDATA (arg), &color,
					     true, false))
    return color.pixel;
  return default_pixel;
}

static void
remote_set_foreground_color (struct frame *f, Lisp_Object arg,
			     Lisp_Object oldval)
{
  unsigned long pixel = remote_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  FRAME_FOREGROUND_PIXEL (f) = pixel;
  FRAME_REMOTE_OUTPUT (f)->foreground_color = pixel;
  update_face_from_frame_parameter (f, Qforeground_color, arg);
}

static void
remote_set_background_color (struct frame *f, Lisp_Object arg,
			     Lisp_Object oldval)
{
  unsigned long pixel = remote_decode_color (f, arg, WHITE_PIX_DEFAULT (f));
  FRAME_BACKGROUND_PIXEL (f) = pixel;
  FRAME_REMOTE_OUTPUT (f)->background_color = pixel;
  update_face_from_frame_parameter (f, Qbackground_color, arg);
}

/* The handler of the `cursor-color' frame parameter.  A cursor in the
   background color is not visible, so use the foreground color for
   it.  The glyph under a box has the background color.  */
static void
remote_set_cursor_color (struct frame *f, Lisp_Object arg,
			 Lisp_Object oldval)
{
  unsigned long pixel = remote_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  if (pixel == FRAME_BACKGROUND_PIXEL (f))
    pixel = FRAME_FOREGROUND_PIXEL (f);
  FRAME_REMOTE_OUTPUT (f)->cursor_color = pixel;
  FRAME_REMOTE_OUTPUT (f)->cursor_foreground_color
    = FRAME_BACKGROUND_PIXEL (f);
}


/***********************************************************************
				Frames
 ***********************************************************************/

/* The handler of the `cursor-type' frame parameter.  */
static void
remote_set_cursor_type (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  set_frame_cursor_types (f, arg);
}

/* The handler of the `menu-bar-lines' frame parameter.  Set the
   height of the menu bar of F to VALUE lines.  A VALUE that is not a
   natural number gives no menu bar.  Redisplay draws the menu bar into
   the menu bar window of F.  A minibuffer-only frame and a child frame
   have no menu bar.  */
static void
remote_set_menu_bar_lines (struct frame *f, Lisp_Object value,
			   Lisp_Object oldval)
{
  if (FRAME_MINIBUF_ONLY_P (f) || FRAME_PARENT_FRAME (f))
    return;

  int old_lines = FRAME_MENU_BAR_LINES (f);
  int lines = RANGED_FIXNUMP (0, value, INT_MAX) ? XFIXNUM (value) : 0;

  fset_redisplay (f);
  FRAME_MENU_BAR_LINES (f) = lines;
  FRAME_MENU_BAR_HEIGHT (f) = lines * FRAME_LINE_HEIGHT (f);
  if (lines != old_lines)
    adjust_frame_size (f, -1, -1, 3, true, Qmenu_bar_lines);
}

/* The handlers of frame parameters, in the order of frame_parms in
   frame.c.  NULL means that the parameter has no effect yet.  */
static frame_parm_handler remote_frame_parm_handlers[] =
  {
    gui_set_autoraise,
    gui_set_autolower,
    remote_set_background_color,
    NULL, /* border color */
    NULL, /* border width: the client draws no outer border */
    remote_set_cursor_color,
    remote_set_cursor_type,
    gui_set_font,
    remote_set_foreground_color,
    NULL, /* icon name */
    NULL, /* icon type */
    NULL, /* child frame border width */
    NULL, /* internal border width */
    gui_set_right_divider_width,
    gui_set_bottom_divider_width,
    remote_set_menu_bar_lines,
    NULL, /* mouse color */
    NULL, /* explicitly set name */
    gui_set_scroll_bar_width,
    gui_set_scroll_bar_height,
    NULL, /* title */
    gui_set_unsplittable,
    gui_set_vertical_scroll_bars,
    gui_set_horizontal_scroll_bars,
    gui_set_visibility,
    NULL, /* tab bar lines */
    NULL, /* tool bar lines */
    NULL, /* scroll bar foreground */
    NULL, /* scroll bar background */
    gui_set_screen_gamma,
    gui_set_line_spacing,
    gui_set_left_fringe,
    gui_set_right_fringe,
    NULL, /* wait for window manager */
    gui_set_fullscreen,
    gui_set_font_backend,
    NULL, /* alpha */
    NULL, /* sticky */
    NULL, /* tool bar position */
    NULL, /* inhibit double buffering */
    NULL, /* undecorated */
    remote_set_parent_frame,
    NULL, /* skip taskbar */
    NULL, /* no focus on map */
    NULL, /* no accept focus */
    NULL, /* z group */
    NULL, /* override redirect */
    gui_set_no_special_glyphs,
    gui_set_alpha_background,
    gui_set_borders_respect_alpha_background,
    NULL, /* use frame synchronization */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
  };

static void
remote_unwind_create_frame (Lisp_Object frame)
{
  struct frame *f = XFRAME (frame);
  if (!FRAME_LIVE_P (f))
    return;
  if (NILP (Fmemq (frame, Vframe_list)))
    {
      /* The frame is not complete, so `delete_frame' does not run.
	 Free what it frees.  */
      remote_free_frame_resources (f);
      free_glyphs (f);
      free_font_driver_list (f);
      xfree (f->decode_mode_spec_buffer);
    }
}

/* Make a frame on the session that PARMS names.  */
static Lisp_Object
remote_create_frame (Lisp_Object parms)
{
  specpdl_ref count = SPECPDL_INDEX ();
  parms = Fcopy_alist (parms);
  Vx_resource_name = Vinvocation_name;

  struct remote_display_info *dpyinfo
    = check_x_display_info (remote_frame_session_arg (parms));
  Lisp_Object parent = remote_parent_frame_arg (dpyinfo, parms);
  struct frame *f = remote_make_frame_on (dpyinfo, parms);
  Lisp_Object frame;
  XSETFRAME (frame, f);
  record_unwind_protect (remote_unwind_create_frame, frame);

  remote_set_initial_parent (f, parent);
  remote_init_frame_font (f, parms);
  remote_default_frame_parameters (f, parms);
  remote_finish_frame (f, parms);
  remote_calc_absolute_position (f);
  return unbind_to (count, frame);
}

/* Return the value of the `terminal' or `display' parameter in PARMS,
   or nil.  */
static Lisp_Object
remote_frame_session_arg (Lisp_Object parms)
{
  Lisp_Object display = gui_display_get_arg (NULL, parms, Qterminal, 0, 0,
					     RES_TYPE_NUMBER);
  if (BASE_EQ (display, Qunbound) || NILP (display))
    display = gui_display_get_arg (NULL, parms, Qdisplay, 0, 0,
				   RES_TYPE_STRING);
  return BASE_EQ (display, Qunbound) ? Qnil : display;
}

/* Return a new frame of the session DPYINFO, without its font and
   parameters.  */
static struct frame *
remote_make_frame_on (struct remote_display_info *dpyinfo, Lisp_Object parms)
{
  struct frame *f = remote_make_frame_with_minibuffer (dpyinfo, parms);
  frame_set_id_from_params (f, parms);
  f->terminal = dpyinfo->terminal;
  f->output_method = output_remote;
  f->output_data.remote = xzalloc (sizeof *f->output_data.remote);
  FRAME_DISPLAY_INFO (f) = dpyinfo;
  remote_assign_frame_id (f);
  remote_forget_place (f);
  FRAME_FONTSET (f) = -1;
  fset_name (f, Vinvocation_name);
  f->explicit_name = false;
  return f;
}

/* Return the frame that the parent-frame parameter in PARMS names, or
   nil if PARMS give no parent.  Signal an error if the parent is not a
   live frame of the session DPYINFO.  */
static Lisp_Object
remote_parent_frame_arg (struct remote_display_info *dpyinfo,
			 Lisp_Object parms)
{
  Lisp_Object parent = gui_display_get_arg (dpyinfo, parms, Qparent_frame,
					    NULL, NULL, RES_TYPE_SYMBOL);
  if (BASE_EQ (parent, Qunbound) || NILP (parent))
    return Qnil;
  if (!remote_session_frame_p (parent, dpyinfo))
    error ("The parent frame must be a live frame of the same session");
  return parent;
}

/* Return true if FRAME is a live frame of the session DPYINFO.  */
static bool
remote_session_frame_p (Lisp_Object frame, struct remote_display_info *dpyinfo)
{
  return (FRAMEP (frame)
	  && FRAME_LIVE_P (XFRAME (frame))
	  && FRAME_REMOTE_P (XFRAME (frame))
	  && FRAME_DISPLAY_INFO (XFRAME (frame)) == dpyinfo);
}

/* Return a new frame with the minibuffer window that PARMS ask for.
   With no minibuffer parameter or a value that is not nil, not `only'
   and not a window, the frame has a minibuffer window of its own.  With
   nil, the frame uses the minibuffer window of the default minibuffer
   frame of its session.  With a window, the frame uses that window,
   which must be on the same session.  With `only', the frame has only a
   minibuffer window.  */
static struct frame *
remote_make_frame_with_minibuffer (struct remote_display_info *dpyinfo,
				   Lisp_Object parms)
{
  Lisp_Object minibuffer = gui_display_get_arg (dpyinfo, parms, Qminibuffer,
						NULL, NULL, RES_TYPE_SYMBOL);
  if (NILP (minibuffer) || WINDOWP (minibuffer))
    return make_frame_without_minibuffer (minibuffer,
					  dpyinfo->terminal->kboard,
					  XCAR (dpyinfo->name_list_element));
  if (EQ (minibuffer, Qonly))
    return make_minibuffer_frame ();
  return make_frame (true);
}

/* Make PARENT the parent of the new frame F, above each sibling of F.
   PARENT nil means that F is a root frame.  Do this before the size
   and the position of F, because a relative position uses the size of
   the parent.  */
static void
remote_set_initial_parent (struct frame *f, Lisp_Object parent)
{
  fset_parent_frame (f, parent);
  store_frame_param (f, Qparent_frame, parent);
  if (!NILP (parent))
    f->z_order = 1 + max_child_z_order (XFRAME (parent));
}

/* Give F the next frame ID of its session.  The counter wraps at the
   end of the range.  Skip 0, because the generic code reads a frame
   with the ID 0 as a frame with no window.  Skip the IDs of the live
   frames, because the client knows a frame by its ID.  */
static void
remote_assign_frame_id (struct frame *f)
{
  struct remote_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  do
    dpyinfo->last_frame_id++;
  while (!dpyinfo->last_frame_id
	 || remote_frame_id_used_p (dpyinfo, dpyinfo->last_frame_id));
  FRAME_NATIVE_WINDOW (f) = dpyinfo->last_frame_id;
}

/* Return true if a live frame of DPYINFO has the frame ID ID.  */
static bool
remote_frame_id_used_p (struct remote_display_info *dpyinfo, uint32_t id)
{
  return remote_frame_of_id (dpyinfo, id) != NULL;
}

/* Return the frame of DPYINFO with the ID, or NULL if it has none.  */
struct frame *
remote_frame_of_id (struct remote_display_info *dpyinfo, uint32_t id)
{
  Lisp_Object tail, frame;
  FOR_EACH_FRAME (tail, frame)
    if (FRAME_REMOTE_P (XFRAME (frame))
	&& FRAME_DISPLAY_INFO (XFRAME (frame)) == dpyinfo
	&& remote_frame_id (XFRAME (frame)) == id)
      return XFRAME (frame);
  return NULL;
}

/* Return the ID by which the client knows F.  */
static uint32_t
remote_frame_id (struct frame *f)
{
  return FRAME_NATIVE_WINDOW (f);
}

/* Give F its font drivers and its default font.  */
static void
remote_init_frame_font (struct frame *f, Lisp_Object parms)
{
  register_font_driver (&ftcrfont_driver, f);
#ifdef HAVE_HARFBUZZ
  register_font_driver (&ftcrhbfont_driver, f);
#endif
  gui_default_parameter (f, parms, Qfont_backend, Qnil,
			 "fontBackend", "FontBackend", RES_TYPE_STRING);
  FRAME_RIF (f)->default_font_parameter (f, parms);
  if (!FRAME_FONT (f))
    {
      Lisp_Object frame;
      XSETFRAME (frame, f);
      delete_frame (frame, Qnoelisp);
      error ("Invalid frame font");
    }
}

/* Set the parameters of F that PARMS does not give to their default
   values, then set the faces and the size of F.  */
static void
remote_default_frame_parameters (struct frame *f, Lisp_Object parms)
{
  gui_default_parameter (f, parms, Qborder_width, make_fixnum (0),
			 "borderwidth", "BorderWidth", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qright_divider_width, make_fixnum (0),
			 NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qbottom_divider_width, make_fixnum (0),
			 NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qvertical_scroll_bars,
			 Vdefault_frame_scroll_bars,
			 "verticalScrollBars", "VerticalScrollBars",
			 RES_TYPE_SYMBOL);
  gui_default_parameter (f, parms, Qhorizontal_scroll_bars, Qnil,
			 "horizontalScrollBars", "HorizontalScrollBars",
			 RES_TYPE_SYMBOL);
  gui_default_parameter (f, parms, Qforeground_color, build_string ("black"),
			 "foreground", "Foreground", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qbackground_color, build_string ("white"),
			 "background", "Background", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qline_spacing, Qnil,
			 "lineSpacing", "LineSpacing", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qleft_fringe, Qnil,
			 "leftFringe", "LeftFringe", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qright_fringe, Qnil,
			 "rightFringe", "RightFringe", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qno_special_glyphs, Qnil,
			 NULL, NULL, RES_TYPE_BOOLEAN);

  init_frame_faces (f);

  adjust_frame_size (f, FRAME_COLS (f) * FRAME_COLUMN_WIDTH (f),
		     FRAME_LINES (f) * FRAME_LINE_HEIGHT (f), 5, true,
		     Qx_create_frame_1);

  gui_default_parameter (f, parms, Qmenu_bar_lines,
			 NILP (Vmenu_bar_mode)
			 ? make_fixnum (0) : make_fixnum (1),
			 NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qtab_bar_lines,
			 NILP (Vtab_bar_mode)
			 ? make_fixnum (0) : make_fixnum (1),
			 NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qtool_bar_lines, make_fixnum (0),
			 NULL, NULL, RES_TYPE_NUMBER);

  gui_figure_window_size (f, parms, false, true);

}

/* Make the frame F a member of the frame list.  Show it, unless the
   visibility parameter in PARMS is nil.  */
static void
remote_finish_frame (struct frame *f, Lisp_Object parms)
{
  Lisp_Object frame;
  XSETFRAME (frame, f);
  struct kboard *kb = FRAME_TERMINAL (f)->kboard;
  f->terminal->reference_count++;
  Vframe_list = Fcons (frame, Vframe_list);

  gui_default_parameter (f, parms, Qcursor_type, Qbox,
			 "cursorType", "CursorType", RES_TYPE_SYMBOL);
  gui_default_parameter (f, parms, Qscroll_bar_width, Qnil,
			 "scrollBarWidth", "ScrollBarWidth", RES_TYPE_NUMBER);

  f->can_set_window_size = true;
  adjust_frame_size (f, FRAME_TEXT_WIDTH (f), FRAME_TEXT_HEIGHT (f),
		     0, true, Qx_create_frame_2);

  SET_FRAME_VISIBLE (f, remote_initially_visible_p (parms));

  if (FRAME_HAS_MINIBUF_P (f)
      && (!FRAMEP (KVAR (kb, Vdefault_minibuffer_frame))
	  || !FRAME_LIVE_P (XFRAME (KVAR (kb, Vdefault_minibuffer_frame)))))
    kset_default_minibuffer_frame (kb, frame);

  for (Lisp_Object tem = parms; CONSP (tem); tem = XCDR (tem))
    if (CONSP (XCAR (tem)) && !NILP (XCAR (XCAR (tem))))
      fset_param_alist (f, Fcons (XCAR (tem), f->param_alist));

  Vwindow_list = Qnil;
}

/* Return false if the visibility parameter in PARMS is nil, else true.
   x-create-frame-with-faces gives nil, and then makes the frame
   visible when it is complete.  */
static bool
remote_initially_visible_p (Lisp_Object parms)
{
  return !NILP (gui_display_get_arg (NULL, parms, Qvisibility, NULL, NULL,
				     RES_TYPE_SYMBOL));
}

/* The default_font_parameter of the redisplay interface.  */
static void
remote_default_font_parameter (struct frame *f, Lisp_Object parms)
{
  Lisp_Object font = gui_display_get_arg (FRAME_DISPLAY_INFO (f), parms,
					  Qfont, "font", "Font",
					  RES_TYPE_STRING);
  if (BASE_EQ (font, Qunbound) || NILP (font))
    font = font_open_by_name (f, build_unibyte_string ("monospace-10"));
  if (NILP (font))
    error ("No font is available for the remote frame");
  gui_default_parameter (f, parms, Qfont, font, "font", "Font",
			 RES_TYPE_STRING);
}

/* The smallest default width of a scroll bar, in pixels.  This is the
   width of an X scroll bar with no toolkit.  */
enum { REMOTE_SCROLL_BAR_MIN_DEFAULT_WIDTH = 14 };

/* Set the scroll-bar width of F for the column width of F.  A scroll
   bar of the default width gets the default width.  A scroll bar of a
   given width (the scroll-bar-width parameter) keeps that width, in
   whole columns.  */
static void
remote_update_scroll_bar_width (struct frame *f)
{
  int unit = FRAME_COLUMN_WIDTH (f);
  if (RANGED_FIXNUMP (1, get_frame_param (f, Qscroll_bar_width), INT_MAX))
    FRAME_CONFIG_SCROLL_BAR_COLS (f)
      = (FRAME_CONFIG_SCROLL_BAR_WIDTH (f) + unit - 1) / unit;
  else
    remote_set_scroll_bar_default_width (f);
}

/* The set_scroll_bar_default_width_hook.  The default width is the
   smallest number of whole columns that is at least
   REMOTE_SCROLL_BAR_MIN_DEFAULT_WIDTH pixels.  */
static void
remote_set_scroll_bar_default_width (struct frame *f)
{
  int unit = FRAME_COLUMN_WIDTH (f);
  FRAME_CONFIG_SCROLL_BAR_COLS (f)
    = (REMOTE_SCROLL_BAR_MIN_DEFAULT_WIDTH + unit - 1) / unit;
  FRAME_CONFIG_SCROLL_BAR_WIDTH (f) = FRAME_CONFIG_SCROLL_BAR_COLS (f) * unit;
}

/* The set_new_font_hook.  */
static Lisp_Object
remote_new_font (struct frame *f, Lisp_Object font_object, int fontset)
{
  struct font *font = XFONT_OBJECT (font_object);
  if (fontset < 0)
    fontset = fontset_from_font (font_object);
  FRAME_FONTSET (f) = fontset;
  if (FRAME_FONT (f) == font)
    return font_object;

  FRAME_FONT (f) = font;
  FRAME_BASELINE_OFFSET (f) = font->baseline_offset;
  FRAME_COLUMN_WIDTH (f) = font->average_width;
  int ascent, descent;
  get_font_ascent_descent (font, &ascent, &descent);
  FRAME_LINE_HEIGHT (f) = ascent + descent;
  FRAME_TAB_BAR_HEIGHT (f) = FRAME_TAB_BAR_LINES (f) * FRAME_LINE_HEIGHT (f);

  remote_update_scroll_bar_width (f);

  if (f->can_set_window_size)
    adjust_frame_size (f, FRAME_COLS (f) * FRAME_COLUMN_WIDTH (f),
		       FRAME_LINES (f) * FRAME_LINE_HEIGHT (f),
		       3, false, Qfont);
  return font_object;
}

/* The set_window_size_hook.  No client asks for a different size, so
   the new size takes effect at once.  */
static void
remote_set_window_size (struct frame *f, bool change_gravity,
			int width, int height)
{
  change_frame_size (f, width, height, false, false, false);
}

/* The delete_frame_hook.  */
static void
remote_delete_frame (struct frame *f)
{
  remote_send_delete_frame (f);
  remote_flush_frame (f);
  remote_free_frame_resources (f);
}

/* Remove F from the mouse state of DPYINFO: the frame of the last
   motion, the frame of the mouse highlight, and the frame of the
   scroll-bar grab.  */
static void
remote_forget_mouse_frame (struct remote_display_info *dpyinfo,
			   struct frame *f)
{
  if (dpyinfo->last_mouse_motion_frame == f)
    dpyinfo->last_mouse_motion_frame = NULL;
  remote_scroll_bar_forget_frame (&dpyinfo->scroll_bar_grab, f);
  if (dpyinfo->mouse_highlight.mouse_face_mouse_frame == f)
    reset_mouse_highlight (&dpyinfo->mouse_highlight);
}

/* Free the data of F that the generic code does not free, and remove
   F from its session.  */
static void
remote_free_frame_resources (struct frame *f)
{
  struct remote_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  block_input ();
  free_frame_faces (f);
  if (dpyinfo->focus_frame == f)
    dpyinfo->focus_frame = NULL;
  if (dpyinfo->highlight_frame == f)
    dpyinfo->highlight_frame = NULL;
  remote_forget_mouse_frame (dpyinfo, f);
  xfree (f->output_data.remote);
  f->output_data.remote = NULL;
  unblock_input ();
}

/* Give F the focus of DPYINFO.  F is NULL if no frame of DPYINFO has
   the focus.  */
void
remote_new_focus_frame (struct remote_display_info *dpyinfo, struct frame *f)
{
  dpyinfo->focus_frame = f;
  remote_frame_rehighlight (dpyinfo);
}

/* The frame_rehighlight_hook.  The focus redirection of a frame of
   the session of F changed.  */
static void
remote_frame_rehighlight_hook (struct frame *f)
{
  remote_frame_rehighlight (FRAME_DISPLAY_INFO (f));
}

/* Make the highlight frame of DPYINFO the frame that gets the input of
   its focus frame.  If the highlight frame changes, draw the cursors of
   the old and the new highlight frame again.  */
static void
remote_frame_rehighlight (struct remote_display_info *dpyinfo)
{
  struct frame *old = dpyinfo->highlight_frame;
  dpyinfo->highlight_frame = remote_input_frame (dpyinfo,
						 dpyinfo->focus_frame);
  if (old == dpyinfo->highlight_frame)
    return;
  if (old)
    remote_redraw_cursors (old);
  if (dpyinfo->highlight_frame)
    remote_redraw_cursors (dpyinfo->highlight_frame);
}

/* Return the frame of DPYINFO that gets the input of the focus frame F:
   the live frame of the focus redirection of F, if it is a frame of
   DPYINFO, or else F.  Return NULL if F is NULL.  */
static struct frame *
remote_input_frame (struct remote_display_info *dpyinfo, struct frame *f)
{
  if (!f)
    return NULL;
  Lisp_Object redirect = FRAME_FOCUS_FRAME (f);
  if (FRAMEP (redirect) && FRAME_LIVE_P (XFRAME (redirect))
      && FRAME_REMOTE_P (XFRAME (redirect))
      && FRAME_DISPLAY_INFO (XFRAME (redirect)) == dpyinfo)
    return XFRAME (redirect);
  return f;
}

/* Draw the cursors of F again, and send them to the client.  The type
   of a cursor depends on the highlight frame.  */
static void
remote_redraw_cursors (struct frame *f)
{
  block_input ();
  gui_update_cursor (f, true);
  remote_flush_frame (f);
  unblock_input ();
}


/***********************************************************************
		    Hooks that need a client to act
 ***********************************************************************/

/* These hooks do nothing until a client can receive their output.  */

static void remote_no_op_frame (struct frame *f) { }

void
frame_set_mouse_pixel_position (struct frame *f, int pix_x, int pix_y)
{
}

char *
get_keysym_name (int keysym)
{
  return NULL;
}

static int
remote_read_socket (struct terminal *terminal, struct input_event *hold)
{
  return 0;
}

static void
remote_free_pixmap (struct frame *f, Emacs_Pixmap pixmap)
{
  if (pixmap)
    {
      xfree (pixmap->data);
      xfree (pixmap);
    }
}

static const char *
remote_get_string_resource (void *rdb, const char *name, const char *class)
{
  return NULL;
}

/* Copy the pixels of RUN in W from their current position to their
   desired position.  A copy-area message has no clip, so send only the
   rows that stay in the text area of W.  */
static void
remote_scroll_run (struct window *w, struct run *run)
{
  int x, y, width, height;
  window_box (w, ANY_AREA, &x, &y, &width, &height);
  int from_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->current_y);
  int to_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->desired_y);
  int rows = remote_clip_copy (y, y + height, &from_y, &to_y, run->height);
  if (remote_empty_area_p (width, rows))
    return;
  struct frame *f = XFRAME (w->frame);

  /* Erase the cursor, so that the copy does not move it.  Redisplay
     draws it again at the end of the update of W.  */
  gui_clear_cursor (w);

  struct rd_message message =
    {
      .type = RD_COPY_AREA,
      .u.copy_area = { .frame = remote_frame_id (f), .x = x, .y = from_y,
		       .width = width, .height = rows, .to_x = x,
		       .to_y = to_y },
    };
  remote_send_draw (f, &message);
}

/* Return the number of the HEIGHT rows from *FROM that can go to *TO in
   a text area from TOP to the row before BOTTOM, and move *FROM and *TO
   to the first of those rows.  The return value is 0 or less if no row
   can go.

   The rows above TOP at the destination must go.  After a change of
   the header line, a row cut at the top keeps its visible height and
   changes its position, so redisplay can move a row that starts above
   the window.  The source of the rows that stay needs no clip at the
   top, because they were text before the change, and redisplay draws
   the header line after the copy.

   The callers in redisplay give a run that ends at BOTTOM or above it,
   because they compute the run from the same `window_text_bottom_y'.
   A row below BOTTOM is the mode line or another window, and a copy has
   no clip, so this function does not trust the caller.  It stops
   the source and the destination at BOTTOM, as `pgtk_scroll_run' does.

   The values of a run are not limited, so compute in a wider type.  */
static int
remote_clip_copy (int top, int bottom, int *from, int *to, int height)
{
  intmax_t above = max (0, (intmax_t) top - *to);
  intmax_t source = *from + above;
  intmax_t destination = *to + above;
  intmax_t rows = min (height - above, bottom - max (source, destination));
  if (rows <= 0)
    return 0;
  *from = source;
  *to = destination;
  return rows;
}

/* Mark DESIRED_ROW of W so that its fringes are drawn again.  The row
   of a new or a deleted window can have the same position and the same
   bitmap as the row before it, so the fringe code cannot find that the
   client shows something else there.  A remote frame has no internal
   border, so there is no border to clear.  */
static void
remote_after_update_window_line (struct window *w,
				 struct glyph_row *desired_row)
{
  desired_row->redraw_fringe_bitmaps_p = true;
}

/* Clear the fringe area of P if it has one, and send the rows of the
   bitmap of P that are visible in ROW.  The message has no clip, and
   the client draws all of its rows.  So a bitmap on a line visible in
   part, or a bitmap taller than ROW, must lose its other rows here.
   The fringe code keeps the bitmap in the width of the fringe.  */
static void
remote_draw_fringe_bitmap (struct window *w, struct glyph_row *row,
			   struct draw_fringe_bitmap_params *p)
{
  struct frame *f = XFRAME (w->frame);
  if (p->bx >= 0 && !p->overlay_p)
    remote_fill_area (f, p->bx, p->by, p->nx, p->ny, p->face->background);
  int first, end;
  remote_fringe_visible_rows (w, row, p, &first, &end);
  if (!remote_fringe_fits_p (p, first, end))
    return;

  /* Do not initialize the message: that fills all rows, and the
     encoder reads only NROWS of them.  */
  struct rd_message message;
  message.type = RD_DRAW_FRINGE;
  struct rd_draw_fringe *m = &message.u.draw_fringe;
  m->frame = remote_frame_id (f);
  m->x = p->x;
  m->y = p->y + first;
  m->width = p->wd;
  m->overlay = p->overlay_p;
  m->cursor = p->cursor_p;
  m->foreground = remote_fringe_foreground (f, p);
  m->background = p->face->background;
  m->nrows = end - first;

  /* A fringe that is narrower than the bitmap shows the left pixels of
     the bitmap, and bit 0 of a row of the message is the rightmost
     pixel that the client shows.  */
  int right_pixels = p->bitmap_wd - p->wd;
  for (int i = 0; i < m->nrows; i++)
    m->rows[i] = p->bits[p->dh + first + i] >> right_pixels;
  remote_send_draw (f, &message);
}

/* Set *FIRST and *END to the first row of P to send, and to the row
   after the last one.  The range is empty if no row of P is visible
   in ROW of W.  */
static void
remote_fringe_visible_rows (struct window *w, struct glyph_row *row,
			    const struct draw_fringe_bitmap_params *p,
			    int *first, int *end)
{
  int top, bottom;
  remote_row_bounds (w, row, &top, &bottom);
  *first = max (remote_fringe_rows_above (p), top - p->y);
  *end = min (p->h, bottom - p->y);
}

/* Return true if a draw-fringe message can hold the rows of P from
   FIRST to the row before END.  */
static bool
remote_fringe_fits_p (const struct draw_fringe_bitmap_params *p,
		      int first, int end)
{
  return (0 < p->wd && p->wd <= UINT16_MAX
	  && first < end && end - first <= RD_MAX_FRINGE_ROWS);
}

/* Return the color of the bits of P that are 1.  A cursor in the
   fringe has the cursor color, and an overlay cursor has the
   background of the face, as in pgtk_draw_fringe_bitmap.  */
static unsigned long
remote_fringe_foreground (struct frame *f,
			  const struct draw_fringe_bitmap_params *p)
{
  if (!p->cursor_p)
    return p->face->foreground;
  return (p->overlay_p ? p->face->background
	  : FRAME_REMOTE_OUTPUT (f)->cursor_color);
}

/* Return the number of rows of P above the first row of its bitmap.
   A periodic bitmap on a line that starts above the frame has a
   negative row offset, and the fringe code adds the same number of
   rows to its height.  Those rows have no pixels.  */
static int
remote_fringe_rows_above (const struct draw_fringe_bitmap_params *p)
{
  return max (0, -p->dh);
}

/* Clear the area of S, and send its glyphs or its image if the
   protocol can show them.  A client draws only the glyphs of a
   message, so the clear erases a cursor or older text under S.  The
   protocol has no message for a glyphless character, so the client
   shows the background there.  */
static void
remote_draw_glyph_string (struct glyph_string *s)
{
  /* A string for the overhang of a neighbor draws over pixels that are
     correct, so it must not clear them.  A string under a box cursor
     must not clear the box.  */
  if (!s->for_overlaps && s->hl != DRAW_CURSOR)
    remote_clear_glyph_string (s);
  if (s->first_glyph->type == CHAR_GLYPH)
    remote_draw_chars (s);
  else if (s->first_glyph->type == COMPOSITE_GLYPH)
    remote_draw_composition (s);
  else if (s->first_glyph->type == IMAGE_GLYPH)
    remote_draw_image (s);
}

/* Draw S, a string of one slice of an image, as
   pgtk_draw_image_foreground does.  Send the data of the image, then a
   draw-image message for the slice.  If the protocol cannot send the
   image (it did not load, or it is too large), draw the outline of the
   slice in the foreground of S.  */
static void
remote_draw_image (struct glyph_string *s)
{
  Emacs_Rectangle slice = remote_image_slice_area (s);
  uint32_t id = remote_image_prepare (s->f, s->img);
  if (id)
    remote_send_image (s, id, slice);
  else
    remote_draw_outline (s->f, slice,
			 remote_glyph_string_colors (s).foreground);
}

/* Return the rectangle of the slice of the image of S in its frame.
   The left line of a box and the margin of the image come before the
   first column of slices, and the margin comes above the first row.  */
static Emacs_Rectangle
remote_image_slice_area (struct glyph_string *s)
{
  int x = s->x;
  int y = s->ybase - image_ascent (s->img, s->face, &s->slice);
  if (s->slice.x == 0)
    x = remote_foreground_x (s) + s->img->hmargin;
  if (s->slice.y == 0)
    y += s->img->vmargin;
  return (Emacs_Rectangle) { x, y, s->slice.width, s->slice.height };
}

/* Send a draw-image message for the image ID of S, whose slice is at
   SLICE.  The message has the position of the whole image, and the
   clip lets it change only the pixels of the slice that S can
   change.  */
static void
remote_send_image (struct glyph_string *s, uint32_t id,
		   Emacs_Rectangle slice)
{
  Emacs_Rectangle clip = remote_intersection (remote_glyph_clip (s), slice);
  if (remote_empty_area_p (clip.width, clip.height))
    return;
  struct rd_message message =
    {
      .type = RD_DRAW_IMAGE,
      .u.draw_image = { .frame = remote_frame_id (s->f), .image = id,
			.x = slice.x - s->slice.x,
			.y = slice.y - s->slice.y,
			.clip_x = clip.x, .clip_y = clip.y,
			.clip_width = clip.width,
			.clip_height = clip.height },
    };
  remote_send_draw (s->f, &message);
}

/* Draw the outline of AREA of F in COLOR, as pgtk_draw_rectangle does:
   a line of one pixel at each edge.  The protocol has no message for a
   line, so each line is a clear-area message.  */
static void
remote_draw_outline (struct frame *f, Emacs_Rectangle area,
		     unsigned long color)
{
  int right = area.x + area.width - 1;
  int bottom = area.y + area.height - 1;
  remote_fill_area (f, area.x, area.y, area.width, 1, color);
  remote_fill_area (f, area.x, bottom, area.width, 1, color);
  remote_fill_area (f, area.x, area.y, 1, area.height, color);
  remote_fill_area (f, right, area.y, 1, area.height, color);
}

/* Fill the part of the background of S that is visible in its row.  */
static void
remote_clear_glyph_string (struct glyph_string *s)
{
  Emacs_Rectangle area = { s->x, s->y, s->background_width, s->height };
  Emacs_Rectangle visible = remote_intersection (remote_glyph_clip (s), area);
  remote_fill_area (s->f, visible.x, visible.y, visible.width, visible.height,
		    remote_glyph_string_colors (s).background);
}

/* Return the colors of S for its highlight, as in
   pgtk_set_glyph_string_gc.  */
static struct remote_colors
remote_glyph_string_colors (struct glyph_string *s)
{
  prepare_face_for_display (s->f, s->face);
  if (s->hl == DRAW_CURSOR)
    return remote_cursor_colors (s->f, s->face);
  return (struct remote_colors) { s->face->foreground, s->face->background };
}

/* Return the colors of a box cursor on a glyph of FACE, as in the
   merge of pgtk_set_cursor_gc.  The background is the color of the
   box, and the foreground is the color of the glyph.  The two colors
   differ, also if the face has the cursor color.  The fast path of
   pgtk for the default face gives the same colors, so it is not
   here.  */
static struct remote_colors
remote_cursor_colors (struct frame *f, struct face *face)
{
  struct remote_output *output = FRAME_REMOTE_OUTPUT (f);
  unsigned long background = output->cursor_color;
  unsigned long foreground = face->background;
  if (foreground == background)
    foreground = face->foreground;
  if (foreground == background)
    foreground = output->cursor_foreground_color;
  /* A box with the colors of the face is not visible, so use the
     reverse video of the face.  */
  if (background == face->background && foreground == face->foreground)
    return (struct remote_colors) { face->background, face->foreground };
  return (struct remote_colors) { foreground, background };
}

/* Return the rectangle of the pixels that S can change.  */
static Emacs_Rectangle
remote_glyph_clip (struct glyph_string *s)
{
  NativeRectangle native_clip;
  Emacs_Rectangle clip;
  get_glyph_string_clip_rect (s, &native_clip);
  CONVERT_TO_EMACS_RECT (clip, native_clip);
  return clip;
}

/* Return the part of A inside B.  Its size is 0 if they have no common
   pixels, and each user of the result rejects that size.  */
static Emacs_Rectangle
remote_intersection (Emacs_Rectangle a, Emacs_Rectangle b)
{
  int left = max (a.x, b.x);
  int top = max (a.y, b.y);
  int right = min (a.x + (int) a.width, b.x + (int) b.width);
  int bottom = min (a.y + (int) a.height, b.y + (int) b.height);
  return (Emacs_Rectangle) { left, top, max (right - left, 0),
			     max (bottom - top, 0) };
}

/* Send the glyph codes of S, a string of characters.  A message holds
   at most RD_MAX_GLYPHS glyphs, so send a long glyph string in
   parts.  The width of the first glyph holds the left line of a box,
   so the sum of the widths gives the start of the next part.  */
static void
remote_draw_chars (struct glyph_string *s)
{
  int x = s->x;
  for (int start = 0; start < s->nchars; start += RD_MAX_GLYPHS)
    {
      int n = min (s->nchars - start, RD_MAX_GLYPHS);
      remote_send_glyphs (s, start, n,
			  start ? x : remote_foreground_x (s), s->ybase);
      for (int i = start; i < start + n; i++)
	x += s->first_glyph[i].pixel_width;
    }
}

/* Return the x position of the first glyph of S.  The left line of a
   box is part of S, and the glyphs start after it.  A negative width
   means that the line is inside the glyphs.  */
static int
remote_foreground_x (struct glyph_string *s)
{
  if (s->face->box != FACE_NO_BOX && s->first_glyph->left_box_line_p)
    return s->x + max (s->face->box_vertical_line_width, 0);
  return s->x;
}

/* Send the glyphs of the composition S.  Each glyph has its own
   position, so each one has its own message.  */
static void
remote_draw_composition (struct glyph_string *s)
{
  if (s->first_glyph->u.cmp.automatic)
    remote_draw_automatic_composition (s);
  else
    remote_draw_static_composition (s);
}

/* Send the components of S at their offsets in the composition.  A
   TAB is padding, and has no glyph code.  */
static void
remote_draw_static_composition (struct glyph_string *s)
{
  int x = remote_foreground_x (s);
  for (int i = s->cmp_from; i < s->cmp_to; i++)
    if (COMPOSITION_GLYPH (s->cmp, i) != '\t')
      remote_send_glyphs (s, i, 1, x + s->cmp->offsets[i * 2],
			  s->ybase - s->cmp->offsets[i * 2 + 1]);
}

/* Send the glyphs of S from the shaper.  An adjustment gives the
   offset of a glyph from the pen, and the distance to move the pen.  */
static void
remote_draw_automatic_composition (struct glyph_string *s)
{
  Lisp_Object gstring = composition_gstring_from_id (s->cmp_id);
  int x = remote_foreground_x (s);
  for (int i = s->cmp_from; i < s->cmp_to; i++)
    {
      Lisp_Object glyph = LGSTRING_GLYPH (gstring, i);
      remote_send_glyphs (s, i, 1, x + LGLYPH_XOFF (glyph),
			  s->ybase + LGLYPH_YOFF (glyph));
      x += (NILP (LGLYPH_ADJUSTMENT (glyph))
	    ? LGLYPH_WIDTH (glyph) : LGLYPH_WADJUST (glyph));
    }
}

/* Send the N glyph codes of S from index START, drawn at X, Y.  A glyph
   can extend outside its row, and the area next to the row is ready or
   comes later, so the message has the clip of S.  */
static void
remote_send_glyphs (struct glyph_string *s, int start, int n, int x, int y)
{
  /* Do not initialize the message: that fills all RD_MAX_GLYPHS
     glyphs, and the encoder reads only N of them.  */
  struct rd_message message;
  message.type = RD_DRAW_GLYPHS;
  struct rd_draw_glyphs *m = &message.u.draw_glyphs;
  m->frame = remote_frame_id (s->f);
  m->x = x;
  m->y = y;
  Emacs_Rectangle clip = remote_glyph_clip (s);
  m->clip_x = clip.x;
  m->clip_y = clip.y;
  m->clip_width = clip.width;
  m->clip_height = clip.height;
  m->color = remote_glyph_string_colors (s).foreground;
  Lisp_Object font = remote_font_entry (s->font);
  remote_ensure_font_defined (FRAME_DISPLAY_INFO (s->f), font);
  m->font = remote_font_id (font);
  m->nglyphs = n;
  for (int i = 0; i < n; i++)
    m->glyphs[i] = s->char2b[start + i];
  remote_send_draw (s->f, &message);
}

/* Return the font ID of the font ENTRY.  */
static uint32_t
remote_font_id (Lisp_Object entry)
{
  return XFIXNAT (AREF (entry, 0));
}

/* Send the definition of the font ENTRY to the client of DPYINFO if
   the client does not have it.  */
static void
remote_ensure_font_defined (struct remote_display_info *dpyinfo,
			    Lisp_Object entry)
{
  if (!remote_font_defined_p (dpyinfo, remote_font_id (entry)))
    remote_define_font (dpyinfo, entry);
}

/* Return the entry of FONT, and make it if FONT has none.  */
static Lisp_Object
remote_font_entry (struct font *font)
{
  Lisp_Object font_object;
  XSETFONT (font_object, font);
  Lisp_Object entry = Fgethash (font_object, remote_fonts, Qnil);
  if (!NILP (entry))
    return entry;

  uint32_t file_size = 0;
  Lisp_Object hash = remote_font_hash (font, &file_size);
  Lisp_Object index = make_int (ftcrfont_face_index (font));
  Lisp_Object size = make_int (font->pixel_size);
  Lisp_Object identity = remote_font_identity (font, hash, index, size);
  entry = Fgethash (identity, remote_font_identities, Qnil);
  if (NILP (entry))
    {
      entry = CALLN (Fvector, make_fixnum (++remote_last_font_id),
		     NILP (hash) ? empty_unibyte_string : hash, index, size,
		     make_fixnum (file_size), font->props[FONT_FILE_INDEX]);
      Fputhash (identity, entry, remote_font_identities);
    }
  Fputhash (font_object, entry, remote_fonts);
  return entry;
}

/* Return the identity of FONT, with its HASH, INDEX, and SIZE.  Two
   fonts with the same identity have the same ID.  The client knows a
   font by the hash of its file.  If HASH is nil, the file name of FONT
   takes its place, so two such fonts are the same only if they have
   the same file.  The two kinds of identity have different lengths, so
   they are never equal.  */
static Lisp_Object
remote_font_identity (struct font *font, Lisp_Object hash,
		      Lisp_Object index, Lisp_Object size)
{
  if (NILP (hash))
    return list4 (Qnil, font->props[FONT_FILE_INDEX], index, size);
  return list3 (hash, index, size);
}

/* Return the hash of the file of FONT as a unibyte string, or nil if
   the file cannot be read.  Put the number of bytes of the file in
   *FILE_SIZE.  */
static Lisp_Object
remote_font_hash (struct font *font, uint32_t *file_size)
{
  unsigned char hash[RD_HASH_SIZE];
  if (!remote_file_hash (font->props[FONT_FILE_INDEX], hash, file_size))
    return Qnil;
  return make_unibyte_string ((char *) hash, RD_HASH_SIZE);
}

/* Set HASH to the SHA-256 hash of the bytes of FILE, set *SIZE to
   the number of bytes, and return true.  Return false if FILE is not
   a string or cannot be read, or if its size does not fit in 32 bits,
   the size of the file-size field of a define-font message.  */
bool
remote_file_hash (Lisp_Object file, unsigned char *hash, uint32_t *size)
{
  if (!STRINGP (file))
    return false;
  int fd = emacs_open (SSDATA (ENCODE_FILE (file)), O_RDONLY, 0);
  if (fd < 0)
    return false;
  struct sha256_ctx ctx;
  sha256_init_ctx (&ctx);
  char buffer[16 * 1024];
  ptrdiff_t n;
  uintmax_t total = 0;
  while ((n = emacs_read (fd, buffer, sizeof buffer)) > 0)
    {
      sha256_process_bytes (buffer, n, &ctx);
      total += n;
    }
  emacs_close (fd);
  if (n < 0 || UINT32_MAX < total)
    return false;
  sha256_finish_ctx (&ctx, hash);
  *size = total;
  return true;
}

/* Return the name of the file of a font with the hash HASH, of
   RD_HASH_SIZE bytes, and put its size in *SIZE.  Return nil if no
   font of a session has that hash.  */
Lisp_Object
remote_font_file (const unsigned char *hash, uint32_t *size)
{
  DOHASH (XHASH_TABLE (remote_font_identities), identity, entry)
    if (remote_entry_has_hash (entry, hash))
      {
	*size = XFIXNAT (AREF (entry, 4));
	return AREF (entry, 5);
      }
  return Qnil;
}

/* Forget that the client of DPYINFO has the definitions of the fonts
   with the hash HASH, of RD_HASH_SIZE bytes.  The next draw with such
   a font defines it again.  */
void
remote_forget_fonts (struct remote_display_info *dpyinfo,
		     const unsigned char *hash)
{
  DOHASH (XHASH_TABLE (remote_font_identities), identity, entry)
    {
      uint32_t id = remote_font_id (entry);
      if (remote_entry_has_hash (entry, hash)
	  && id < dpyinfo->font_defined_size)
	dpyinfo->font_defined[id] = false;
    }
}

/* Return true if the font ENTRY has the hash HASH, of RD_HASH_SIZE
   bytes.  */
static bool
remote_entry_has_hash (Lisp_Object entry, const unsigned char *hash)
{
  Lisp_Object entry_hash = AREF (entry, 1);
  return (SBYTES (entry_hash) == RD_HASH_SIZE
	  && !memcmp (SDATA (entry_hash), hash, RD_HASH_SIZE));
}

/* Draw each frame of DPYINFO again at the next redisplay.  */
void
remote_redraw_frames (struct remote_display_info *dpyinfo)
{
  Lisp_Object tail, frame;
  FOR_EACH_FRAME (tail, frame)
    {
      struct frame *f = XFRAME (frame);
      if (FRAME_REMOTE_P (f) && FRAME_DISPLAY_INFO (f) == dpyinfo)
	SET_FRAME_GARBAGED (f);
    }
}

/* Return true if the client of DPYINFO has the definition of the font
   ID.  */
static bool
remote_font_defined_p (struct remote_display_info *dpyinfo, uint32_t id)
{
  return id < dpyinfo->font_defined_size && dpyinfo->font_defined[id];
}

/* Send the definition of the font of ENTRY to the client of DPYINFO,
   and record that the client has it.  An attach clears the record, so
   the record of a session with no client has no effect.  */
static void
remote_define_font (struct remote_display_info *dpyinfo, Lisp_Object entry)
{
  uint32_t id = remote_font_id (entry);
  Lisp_Object hash = AREF (entry, 1);
  struct rd_message message =
    {
      .type = RD_DEFINE_FONT,
      .u.define_font = { .font = id, .index = XFIXNUM (AREF (entry, 2)),
			 .size = XFIXNUM (AREF (entry, 3)),
			 .nhash = SBYTES (hash),
			 .file_size = XFIXNUM (AREF (entry, 4)) },
    };
  memcpy (message.u.define_font.hash, SDATA (hash), SBYTES (hash));
  remote_send_message (dpyinfo, &message);

  if (dpyinfo->font_defined_size <= id)
    {
      ptrdiff_t old_size = dpyinfo->font_defined_size;
      dpyinfo->font_defined
	= xpalloc (dpyinfo->font_defined, &dpyinfo->font_defined_size,
		   id + 1 - old_size, -1, sizeof *dpyinfo->font_defined);
      memset (dpyinfo->font_defined + old_size, 0,
	      dpyinfo->font_defined_size - old_size);
    }
  dpyinfo->font_defined[id] = true;
}

static void
remote_define_frame_cursor (struct frame *f, Emacs_Cursor cursor)
{
}

/* Fill the rectangle at X, Y with the size WIDTH, HEIGHT with the
   background color of F.  */
static void
remote_clear_frame_area (struct frame *f, int x, int y, int width, int height)
{
  remote_fill_area (f, x, y, width, height, FRAME_BACKGROUND_PIXEL (f));
}

/* Fill the rectangle at X, Y with the size WIDTH, HEIGHT of F with
   COLOR.  */
void
remote_fill_area (struct frame *f, int x, int y, int width, int height,
		  unsigned long color)
{
  if (remote_empty_area_p (width, height))
    return;
  struct rd_message message =
    {
      .type = RD_CLEAR_AREA,
      .u.clear_area = { .frame = remote_frame_id (f), .x = x, .y = y,
			.width = width, .height = height, .color = color },
    };
  remote_send_draw (f, &message);
}

/* Draw the cursor of W.  Redisplay erases a cursor by a draw of the
   glyphs under it, so do nothing if ON_P is false.  */
static void
remote_draw_window_cursor (struct window *w, struct glyph_row *glyph_row,
			   int x, int y, enum text_cursor_kinds cursor_type,
			   int cursor_width, bool on_p, bool active_p)
{
  if (!on_p)
    return;
  w->phys_cursor_type = cursor_type;
  w->phys_cursor_on_p = true;
  if (glyph_row->exact_window_width_line_p
      && (glyph_row->reversed_p
	  ? w->phys_cursor.hpos < 0
	  : w->phys_cursor.hpos >= glyph_row->used[TEXT_AREA]))
    {
      glyph_row->cursor_in_fringe_p = true;
      draw_fringe_bitmap (w, glyph_row, glyph_row->reversed_p);
    }
  else if (cursor_type == NO_CURSOR)
    w->phys_cursor_width = 0;
  else if (remote_send_cursor (w, glyph_row, cursor_type, cursor_width)
	   && cursor_type == FILLED_BOX_CURSOR)
    /* A box hides the glyph under it, so draw the glyph again.  */
    draw_phys_cursor_glyph (w, glyph_row, DRAW_CURSOR);
}

/* Send the rectangle of a cursor of CURSOR_TYPE and CURSOR_WIDTH on
   the cursor glyph of ROW in W.  A box covers the glyph, a bar covers
   the height of ROW, and an hbar is at the bottom of ROW.  A cursor
   on a right-to-left glyph is at the right edge of the glyph.  Return
   true if a message went to the client.  */
static bool
remote_send_cursor (struct window *w, struct glyph_row *row,
		    enum text_cursor_kinds cursor_type, int cursor_width)
{
  struct glyph *glyph = get_phys_cursor_glyph (w);
  if (!glyph)
    return false;
  struct frame *f = XFRAME (w->frame);
  int x, y, height;
  get_phys_cursor_geometry (w, row, glyph, &x, &y, &height);
  if (cursor_type == BAR_CURSOR)
    {
      w->phys_cursor_width = min (glyph->pixel_width, cursor_width);
      height = row->height;
    }
  else if (cursor_type == HBAR_CURSOR)
    {
      height = cursor_width;
      y = WINDOW_TO_FRAME_PIXEL_Y (w, (w->phys_cursor.y + row->height
				       - height));
    }
  if ((glyph->resolved_level & 1) != 0)
    x = (WINDOW_TEXT_TO_FRAME_PIXEL_X (w, w->phys_cursor.x)
	 + glyph->pixel_width - w->phys_cursor_width);

  Emacs_Rectangle cursor = { x, y, w->phys_cursor_width, height };
  if (!remote_clip_to_row (w, row, &cursor))
    return false;
  struct rd_message message =
    {
      .type = RD_DRAW_CURSOR,
      .u.draw_cursor = { .frame = remote_frame_id (f), .x = cursor.x,
			 .y = cursor.y, .width = cursor.width,
			 .height = cursor.height,
			 .kind = remote_cursor_kind (cursor_type),
			 .color = remote_cursor_color (f, glyph, cursor_type) },
    };
  remote_send_draw (f, &message);
  return true;
}

/* Return the color of a cursor of CURSOR_TYPE on GLYPH of F.  A bar on
   a face with the cursor color as its background has the foreground of
   the face, as in pgtk_draw_bar_cursor.  */
static unsigned long
remote_cursor_color (struct frame *f, struct glyph *glyph,
		     enum text_cursor_kinds cursor_type)
{
  struct face *face = FACE_FROM_ID (f, glyph->face_id);
  unsigned long color = FRAME_REMOTE_OUTPUT (f)->cursor_color;
  switch (cursor_type)
    {
    case FILLED_BOX_CURSOR:
      prepare_face_for_display (f, face);
      return remote_cursor_colors (f, face).background;
    case BAR_CURSOR:
    case HBAR_CURSOR:
      return face->background == color ? face->foreground : color;
    default:
      return color;
    }
}

/* Reduce RECT to the visible part of ROW in the text area of W.  A
   draw-cursor message has no clip, so a rectangle must not go outside
   the row.
   Return false if no part of RECT is visible.  */
static bool
remote_clip_to_row (struct window *w, struct glyph_row *row,
		    Emacs_Rectangle *rect)
{
  int window_x, window_width, top, bottom;
  window_box (w, TEXT_AREA, &window_x, 0, &window_width, 0);
  remote_row_bounds (w, row, &top, &bottom);
  Emacs_Rectangle clip = { window_x, top, window_width, bottom - top };
  *rect = remote_intersection (clip, *rect);
  return !remote_empty_area_p (rect->width, rect->height);
}

/* Set *TOP and *BOTTOM to the frame row of the top of the visible part
   of ROW in W, and to the row after its bottom.  */
static void
remote_row_bounds (struct window *w, struct glyph_row *row,
		   int *top, int *bottom)
{
  int window_y;
  window_box (w, TEXT_AREA, 0, &window_y, 0, 0);
  *top = max (WINDOW_TO_FRAME_PIXEL_Y (w, row->y), window_y);
  *bottom = *top + row->visible_height;
}

/* Each int position fits in a field of the protocol.  */
verify (INT32_MIN <= INT_MIN && INT_MAX <= INT32_MAX);

/* Return true if a rectangle with the size WIDTH, HEIGHT has no
   pixels.  Redisplay can compute a size of 0 or less.  Send no message
   for that rectangle, because the conversion to an unsigned field of
   the protocol would make a negative size very large.  */
static bool
remote_empty_area_p (int width, int height)
{
  return width <= 0 || height <= 0;
}

/* Return the protocol kind of the visible cursor type CURSOR_TYPE.  */
static enum rd_cursor_kind
remote_cursor_kind (enum text_cursor_kinds cursor_type)
{
  switch (cursor_type)
    {
    case FILLED_BOX_CURSOR:
      return RD_CURSOR_BOX;
    case HOLLOW_BOX_CURSOR:
      return RD_CURSOR_HOLLOW_BOX;
    case BAR_CURSOR:
      return RD_CURSOR_BAR;
    case HBAR_CURSOR:
      return RD_CURSOR_HBAR;
    default:
      emacs_abort ();
    }
}

static void
remote_draw_vertical_window_border (struct window *w, int x, int y0, int y1)
{
}

static void
remote_draw_window_divider (struct window *w, int x0, int x1, int y0, int y1)
{
}

static struct redisplay_interface remote_redisplay_interface =
  {
    remote_frame_parm_handlers,
    gui_produce_glyphs,
    gui_write_glyphs,
    gui_insert_glyphs,
    gui_clear_end_of_line,
    remote_scroll_run,
    remote_after_update_window_line,
    NULL, /* update_window_begin */
    NULL, /* update_window_end */
    remote_flush_frame,
    gui_clear_window_mouse_face,
    gui_get_glyph_overhangs,
    gui_fix_overlapping_area,
    remote_draw_fringe_bitmap,
    NULL, /* define_fringe_bitmap */
    NULL, /* destroy_fringe_bitmap */
    NULL, /* compute_glyph_string_overhangs */
    remote_draw_glyph_string,
    remote_define_frame_cursor,
    remote_clear_frame_area,
    NULL, /* clear_under_internal_border */
    remote_draw_window_cursor,
    remote_draw_vertical_window_border,
    remote_draw_window_divider,
    NULL, /* shift_glyphs_for_insert */
    NULL, /* show_hourglass */
    NULL, /* hide_hourglass */
    remote_default_font_parameter,
  };

static struct terminal *
remote_create_terminal (struct remote_display_info *dpyinfo, Lisp_Object name)
{
  struct terminal *terminal
    = create_terminal (output_remote, &remote_redisplay_interface);
  terminal->display_info.remote = dpyinfo;
  dpyinfo->terminal = terminal;
  terminal->name = xlispstrdup (name);
  terminal->kboard = allocate_kboard (Qremote);
  terminal->kboard->reference_count++;

  terminal->defined_color_hook = remote_defined_color;
  terminal->query_colors = remote_query_colors;
  terminal->query_frame_background_color
    = remote_query_frame_background_color;
  terminal->free_pixmap = remote_free_pixmap;
  terminal->get_string_resource_hook = remote_get_string_resource;
  terminal->menu_show_hook = remote_menu_show;
  terminal->read_socket_hook = remote_read_socket;
  terminal->mouse_position_hook = remote_mouse_position;
  terminal->frame_rehighlight_hook = remote_frame_rehighlight_hook;
  terminal->update_begin_hook = remote_no_op_frame;
  terminal->update_end_hook = remote_update_end;
  terminal->clear_frame_hook = remote_no_op_frame;
  terminal->frame_up_to_date_hook = remote_frame_up_to_date;
  terminal->delete_frame_hook = remote_delete_frame;
  terminal->delete_terminal_hook = remote_delete_terminal;
  terminal->frame_visible_invisible_hook = remote_frame_visible_invisible;
  terminal->set_frame_offset_hook = remote_set_offset;
  terminal->frame_raise_lower_hook = remote_raise_lower_frame;
  terminal->set_window_size_hook = remote_set_window_size;
  terminal->set_new_font_hook = remote_new_font;
  terminal->set_scroll_bar_default_width_hook
    = remote_set_scroll_bar_default_width;
  terminal->set_vertical_scroll_bar_hook = remote_set_vertical_scroll_bar;
  return terminal;
}

void
syms_of_remoteterm (void)
{
  /* The rows of frame_parms depend on the configuration, so only
     frame.c knows their number, and the compiler cannot compare it.
     This check stops Emacs when it starts, so it also stops the dump
     step of the build.  */
  if (countof (remote_frame_parm_handlers) != frame_parm_count)
    fatal ("remote_frame_parm_handlers has %d elements, not %d",
	   (int) countof (remote_frame_parm_handlers), frame_parm_count);

  DEFSYM (Qtrue_color, "true-color");
  /* The frame parameter of a frame that is transparent to input: see
     remoteinput.c.  */
  DEFSYM (Qremote_tip, "remote-tip");
  DEFSYM (Qremote_display__hide_tip, "remote-display--hide-tip");

  remote_display_name_list = Qnil;
  staticpro (&remote_display_name_list);
  remote_color_map = Qnil;
  staticpro (&remote_color_map);
  remote_fonts = CALLN (Fmake_hash_table, QCtest, Qeq, QCweakness, Qkey);
  staticpro (&remote_fonts);
  remote_font_identities = CALLN (Fmake_hash_table, QCtest, Qequal);
  staticpro (&remote_font_identities);

  DEFVAR_BOOL ("x-use-underline-position-properties",
	       x_use_underline_position_properties,
     doc: /* SKIP: real doc in xterm.c.  */);
  x_use_underline_position_properties = true;

  DEFVAR_BOOL ("x-underline-at-descent-line",
	       x_underline_at_descent_line,
     doc: /* SKIP: real doc in xterm.c.  */);
  x_underline_at_descent_line = false;

  /* The remote scroll bar is the scroll bar with no toolkit.
     loadup.el loads scroll-bar.el only if this variable exists, and
     init files call scroll-bar-mode, so the remote backend defines it,
     as the Android port does.  */
  DEFVAR_LISP ("x-toolkit-scroll-bars", Vx_toolkit_scroll_bars,
     doc: /* SKIP: real doc in xterm.c.  */);
  Vx_toolkit_scroll_bars = Qnil;

  defsubr (&Sx_open_connection);
  defsubr (&Sx_close_connection);
  defsubr (&Sx_display_list);
  defsubr (&Sx_create_frame);
  defsubr (&Sxw_display_color_p);
  defsubr (&Sx_display_grayscale_p);
  defsubr (&Sx_hide_tip);
  defsubr (&Sx_display_pixel_width);
  defsubr (&Sx_display_pixel_height);
  defsubr (&Sx_display_mm_width);
  defsubr (&Sx_display_mm_height);
  defsubr (&Sx_display_planes);
  defsubr (&Sx_display_visual_class);
  defsubr (&Sremote_frame_geometry);
  defsubr (&Sremote_frame_edges);
  defsubr (&Sxw_color_defined_p);
  defsubr (&Sxw_color_values);
  defsubr (&Sx_display_color_cells);

  Fprovide (Qremote, Qnil);
}
