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

struct remote_display_info *x_display_list;

/* The name_list_element of each session.  The garbage collector
   reaches those conses through this list.  */
static Lisp_Object remote_display_name_list;

/* The color names of etc/rgb.txt, as (NAME . PIXEL) pairs.  */
static Lisp_Object remote_color_map;

static struct remote_display_info *remote_open_session (Lisp_Object);
static struct remote_display_info *remote_session_by_name (Lisp_Object);
static struct terminal *remote_create_terminal (struct remote_display_info *,
						Lisp_Object);
static Lisp_Object remote_create_frame (Lisp_Object);
static bool remote_terminal_has_frames (struct terminal *);
static Lisp_Object remote_frame_session_arg (Lisp_Object);
static struct frame *remote_make_frame_on (struct remote_display_info *,
					   Lisp_Object);
static void remote_init_frame_font (struct frame *, Lisp_Object);
static void remote_default_frame_parameters (struct frame *, Lisp_Object);
static void remote_finish_frame (struct frame *, Lisp_Object);
static void remote_delete_terminal (struct terminal *);
static bool remote_defined_color (struct frame *, const char *,
				  Emacs_Color *, bool, bool);
static struct redisplay_interface remote_redisplay_interface;


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
  remote_delete_terminal (dpyinfo->terminal);
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

DEFUN ("x-hide-tip", Fx_hide_tip, Sx_hide_tip, 0, 0, 0,
       doc: /* Hide the current tooltip window.
Remote frames have no tooltips yet, so return nil.  */)
  (void)
{
  return Qnil;
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
  xfree (dpyinfo->bitmaps);
  xfree (dpyinfo);
  terminal->display_info.remote = NULL;
  unblock_input ();
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


/***********************************************************************
				Frames
 ***********************************************************************/

/* The handlers of frame parameters, in the order of frame_parms in
   frame.c.  NULL means that the parameter has no effect yet.  */
static frame_parm_handler remote_frame_parm_handlers[] =
  {
    gui_set_autoraise,
    gui_set_autolower,
    remote_set_background_color,
    NULL, /* border color */
    gui_set_border_width,
    NULL, /* cursor color */
    NULL, /* cursor type */
    gui_set_font,
    remote_set_foreground_color,
    NULL, /* icon name */
    NULL, /* icon type */
    NULL, /* child frame border width */
    NULL, /* internal border width */
    gui_set_right_divider_width,
    gui_set_bottom_divider_width,
    NULL, /* menu bar lines */
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
    NULL, /* parent frame */
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
      /* The frame is not complete.  Free it.  */
      xfree (f->output_data.remote);
      f->output_data.remote = NULL;
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
  struct frame *f = remote_make_frame_on (dpyinfo, parms);
  Lisp_Object frame;
  XSETFRAME (frame, f);
  record_unwind_protect (remote_unwind_create_frame, frame);

  remote_init_frame_font (f, parms);
  remote_default_frame_parameters (f, parms);
  remote_finish_frame (f, parms);
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
  struct frame *f = make_frame (true);
  frame_set_id_from_params (f, parms);
  f->terminal = dpyinfo->terminal;
  f->output_method = output_remote;
  f->output_data.remote = xzalloc (sizeof *f->output_data.remote);
  FRAME_DISPLAY_INFO (f) = dpyinfo;
  FRAME_FONTSET (f) = -1;
  fset_name (f, Vinvocation_name);
  f->explicit_name = false;
  return f;
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
  gui_default_parameter (f, parms, Qvertical_scroll_bars, Qnil,
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

/* Make the frame F a member of the frame list, and show it.  */
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

  SET_FRAME_VISIBLE (f, true);

  if (FRAME_HAS_MINIBUF_P (f)
      && (!FRAMEP (KVAR (kb, Vdefault_minibuffer_frame))
	  || !FRAME_LIVE_P (XFRAME (KVAR (kb, Vdefault_minibuffer_frame)))))
    kset_default_minibuffer_frame (kb, frame);

  for (Lisp_Object tem = parms; CONSP (tem); tem = XCDR (tem))
    if (CONSP (XCAR (tem)) && !NILP (XCAR (XCAR (tem))))
      fset_param_alist (f, Fcons (XCAR (tem), f->param_alist));

  Vwindow_list = Qnil;
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

  int unit = FRAME_COLUMN_WIDTH (f);
  if (FRAME_CONFIG_SCROLL_BAR_WIDTH (f) > 0)
    FRAME_CONFIG_SCROLL_BAR_COLS (f)
      = (FRAME_CONFIG_SCROLL_BAR_WIDTH (f) + unit - 1) / unit;
  else
    FRAME_CONFIG_SCROLL_BAR_COLS (f) = (14 + unit - 1) / unit;

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

static void
remote_frame_visible_invisible (struct frame *f, bool visible)
{
  SET_FRAME_VISIBLE (f, visible);
}

/* The delete_frame_hook.  */
static void
remote_delete_frame (struct frame *f)
{
  xfree (f->output_data.remote);
  f->output_data.remote = NULL;
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

static Lisp_Object
remote_menu_show (struct frame *f, int x, int y, int menuflags,
		  Lisp_Object title, const char **error_name)
{
  *error_name = "Menus are not available on remote frames";
  return Qnil;
}

static void
remote_scroll_run (struct window *w, struct run *run)
{
}

static void
remote_after_update_window_line (struct window *w,
				 struct glyph_row *desired_row)
{
}

static void
remote_draw_fringe_bitmap (struct window *w, struct glyph_row *row,
			   struct draw_fringe_bitmap_params *p)
{
}

static void
remote_draw_glyph_string (struct glyph_string *s)
{
}

static void
remote_define_frame_cursor (struct frame *f, Emacs_Cursor cursor)
{
}

static void
remote_clear_frame_area (struct frame *f, int x, int y, int width, int height)
{
}

static void
remote_draw_window_cursor (struct window *w, struct glyph_row *glyph_row,
			   int x, int y, enum text_cursor_kinds cursor_type,
			   int cursor_width, bool on_p, bool active_p)
{
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
    NULL, /* flush_display */
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

  terminal->defined_color_hook = remote_defined_color;
  terminal->query_colors = remote_query_colors;
  terminal->query_frame_background_color
    = remote_query_frame_background_color;
  terminal->free_pixmap = remote_free_pixmap;
  terminal->get_string_resource_hook = remote_get_string_resource;
  terminal->menu_show_hook = remote_menu_show;
  terminal->read_socket_hook = remote_read_socket;
  terminal->update_begin_hook = remote_no_op_frame;
  terminal->update_end_hook = remote_no_op_frame;
  terminal->clear_frame_hook = remote_no_op_frame;
  terminal->frame_up_to_date_hook = remote_no_op_frame;
  terminal->delete_frame_hook = remote_delete_frame;
  terminal->delete_terminal_hook = remote_delete_terminal;
  terminal->frame_visible_invisible_hook = remote_frame_visible_invisible;
  terminal->set_window_size_hook = remote_set_window_size;
  terminal->set_new_font_hook = remote_new_font;
  return terminal;
}

void
syms_of_remoteterm (void)
{
  remote_display_name_list = Qnil;
  staticpro (&remote_display_name_list);
  remote_color_map = Qnil;
  staticpro (&remote_color_map);

  DEFVAR_BOOL ("x-use-underline-position-properties",
	       x_use_underline_position_properties,
     doc: /* SKIP: real doc in xterm.c.  */);
  x_use_underline_position_properties = true;

  DEFVAR_BOOL ("x-underline-at-descent-line",
	       x_underline_at_descent_line,
     doc: /* SKIP: real doc in xterm.c.  */);
  x_underline_at_descent_line = false;

  defsubr (&Sx_open_connection);
  defsubr (&Sx_close_connection);
  defsubr (&Sx_display_list);
  defsubr (&Sx_create_frame);
  defsubr (&Sxw_display_color_p);
  defsubr (&Sx_display_grayscale_p);
  defsubr (&Sx_hide_tip);
  defsubr (&Sxw_color_defined_p);
  defsubr (&Sxw_color_values);
  defsubr (&Sx_display_color_cells);

  Fprovide (Qremote, Qnil);
}
