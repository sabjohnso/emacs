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

/* The operations of the place where a session writes its encoded
   messages.  WRITE takes DATA and the SIZE bytes at BYTES, and it can
   keep the bytes.  FLUSH takes DATA, and sends all the bytes that the
   sink keeps: the session calls it at the end of each update, so an
   update can reach the client as a whole.  FREE takes DATA, and frees
   all that the sink holds.  The sinks of src/remotetest.c are two
   kinds of sink, and each transport is another kind.  */
struct remote_sink_ops
{
  void (*write) (void *data, const unsigned char *bytes, ptrdiff_t size);
  void (*flush) (void *data);
  void (*free) (void *data);
};

struct remote_sink;

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
  Time last_mouse_movement_time;

  /* The ID of the last frame that this session made.  The client
     knows a frame by its ID.  */
  uint32_t last_frame_id;

  /* FONT_DEFINED[ID] is true if the client attached now has the
     definition of the font ID.  The array has FONT_DEFINED_SIZE
     elements, and an ID outside it has no definition.  */
  bool *font_defined;
  ptrdiff_t font_defined_size;

  /* The sink of the client that receives the messages of this
     session.  The session owns the sink.  A client is attached if
     and only if this is not NULL.  While it is NULL, the session sends
     no messages.  */
  struct remote_sink *sink;
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

  /* The frame ID that the client knows.  It has the type of the
     frame field of the protocol, and it is not 0.  */
  uint32_t frame_id;

  /* True if the client knows the frame: the session sent a
     define-frame message for it after the last attach.  The message had
     the size DEFINED_WIDTH, DEFINED_HEIGHT.  */
  bool defined;
  int defined_width;
  int defined_height;

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
#define FRAME_NATIVE_WINDOW(f)    (FRAME_REMOTE_OUTPUT (f)->frame_id)

/* A pixel is 0xRRGGBB.  */
#define RGB_TO_ULONG(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#define RED_FROM_ULONG(color)	(((color) >> 16) & 0xff)
#define GREEN_FROM_ULONG(color)	(((color) >> 8) & 0xff)
#define BLUE_FROM_ULONG(color)	((color) & 0xff)

#define BLACK_PIX_DEFAULT(f) 0
#define WHITE_PIX_DEFAULT(f) 0xffffff

/* Defined in ftcrfont.c.  The remote backend has only the fonts of
   that driver.  */
extern long ftcrfont_face_index (struct font *);

/* Defined in remoteterm.c.  */
extern struct remote_sink *remote_make_sink (const struct remote_sink_ops *,
					     void *);
extern void remote_set_sink (struct remote_display_info *,
			     struct remote_sink *);
extern void *remote_sink_data (struct remote_display_info *,
			       const struct remote_sink_ops *);
extern bool remote_file_hash (Lisp_Object, unsigned char *);
extern void syms_of_remoteterm (void);

/* Defined in remoteproc.c.  */
extern void syms_of_remoteproc (void);

/* In remoteterm.c.  */
extern struct frame *remote_frame_of_id (struct remote_display_info *,
					 uint32_t);
extern void remote_new_focus_frame (struct remote_display_info *,
				    struct frame *);

/* Defined in remotetest.c.  */
extern void syms_of_remotetest (void);

#endif /* EMACS_REMOTETERM_H */
