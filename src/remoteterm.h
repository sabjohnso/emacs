/* Definitions of the remote display backend.

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

/* The remote display backend shows frames on a client program,
   through the remote display protocol (rdproto.h).  A terminal of
   this backend is a session.  The session and its frames continue to
   exist while no client is connected.  */

#ifndef EMACS_REMOTETERM_H
#define EMACS_REMOTETERM_H

#include "dispextern.h"
#include "frame.h"
#include "character.h"
#include "font.h"

#include <cairo.h>

struct remote_bitmap_record
{
  char *file;
  int refcount;
  int height, width, depth;
  cairo_pattern_t *pattern;
};

/* The information about one remote terminal (a session).  */
struct remote_display_info
{
  /* The next session in x_display_list.  */
  struct remote_display_info *next;

  /* The terminal of this session.  */
  struct terminal *terminal;

  /* (NAME . XRM-DATABASE), as for the other window systems.  */
  Lisp_Object name_list_element;

  int n_fonts;
  int smallest_char_width;
  int smallest_font_height;

  struct remote_bitmap_record *bitmaps;
  ptrdiff_t bitmaps_size;
  ptrdiff_t bitmaps_last;

  /* Resolution in dots per inch.  */
  double resx, resy;

  int grabbed;
  int n_planes;
  Window root_window;
  XrmDatabase rdb;
  Emacs_Cursor vertical_scroll_bar_cursor;

  Mouse_HLInfo mouse_highlight;

  struct frame *highlight_frame;
  struct frame *focus_frame;
  struct frame *last_mouse_frame;
  struct frame *last_mouse_motion_frame;
  int last_mouse_motion_x;
  int last_mouse_motion_y;

  /* The ID of the last frame that this session made.  The client
     knows a frame by its ID.  */
  Window last_frame_id;

  /* True if a client receives the messages of this session.  While
     it is false, the session sends no messages.  */
  bool attached;

  /* The encoded messages that the client has not taken.  */
  unsigned char *output;
  ptrdiff_t output_used;
  ptrdiff_t output_size;
};

/* The list of all sessions.  The generic code uses this name.  */
extern struct remote_display_info *x_display_list;

/* The data of one frame of the remote backend.  */
struct remote_output
{
  struct remote_display_info *display_info;

  unsigned long foreground_color;
  unsigned long background_color;
  unsigned long cursor_color;
  unsigned long cursor_foreground_color;
  unsigned long mouse_color;
  unsigned long border_pixel;

  Emacs_Cursor current_cursor;
  Emacs_Cursor text_cursor;
  Emacs_Cursor nontext_cursor;
  Emacs_Cursor modeline_cursor;
  Emacs_Cursor hand_cursor;
  Emacs_Cursor hourglass_cursor;
  Emacs_Cursor horizontal_drag_cursor;
  Emacs_Cursor vertical_drag_cursor;
  Emacs_Cursor left_edge_cursor;
  Emacs_Cursor top_left_corner_cursor;
  Emacs_Cursor top_edge_cursor;
  Emacs_Cursor top_right_corner_cursor;
  Emacs_Cursor right_edge_cursor;
  Emacs_Cursor bottom_right_corner_cursor;
  Emacs_Cursor bottom_edge_cursor;
  Emacs_Cursor bottom_left_corner_cursor;

  /* The frame ID that the client knows.  */
  Window window_desc;
  Window parent_desc;

  struct font *font;
  int baseline_offset;
  int fontset;
};

#define FRAME_REMOTE_OUTPUT(f)    ((f)->output_data.remote)
#define FRAME_OUTPUT_DATA(f)      FRAME_REMOTE_OUTPUT (f)
#define FRAME_DISPLAY_INFO(f)     (FRAME_REMOTE_OUTPUT (f)->display_info)
#define FRAME_FONT(f)             (FRAME_REMOTE_OUTPUT (f)->font)
#define FRAME_FONTSET(f)          (FRAME_REMOTE_OUTPUT (f)->fontset)
#define FRAME_BASELINE_OFFSET(f)  (FRAME_REMOTE_OUTPUT (f)->baseline_offset)
#define FRAME_NATIVE_WINDOW(f)    (FRAME_REMOTE_OUTPUT (f)->window_desc)

/* A pixel is 0xRRGGBB.  */
#define RGB_TO_ULONG(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#define RED_FROM_ULONG(color)	(((color) >> 16) & 0xff)
#define GREEN_FROM_ULONG(color)	(((color) >> 8) & 0xff)
#define BLUE_FROM_ULONG(color)	((color) & 0xff)

#define BLACK_PIX_DEFAULT(f) 0
#define WHITE_PIX_DEFAULT(f) 0xffffff

/* Defined in remoteterm.c.  */
extern void syms_of_remoteterm (void);

#endif /* EMACS_REMOTETERM_H */
