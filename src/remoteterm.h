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

#include "rdchunk.h"

struct rd_message;
struct remote_font_transfer;
struct remote_images;

/* A session sends font data, clipboard data and image data only while
   its sink has fewer bytes than this that it did not send, so these
   large transfers do not delay the draws.  The transport calls
   remote_output_drained when its queue is empty again.  */
enum { REMOTE_PACED_QUEUE_LIMIT = 64 * 1024 };

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
   all that the sink holds.  QUEUED takes DATA.  Each sink must have
   each operation.  The sinks of src/remotetest.c are two kinds of
   sink, and each transport is another kind.  */
struct remote_sink_ops
{
  void (*write) (void *data, const unsigned char *bytes, ptrdiff_t size);
  void (*flush) (void *data);
  void (*free) (void *data);
  /* Return the number of bytes that the sink has and did not send.  */
  ptrdiff_t (*queued) (void *data);
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

  /* The clipboard text that comes from the client, and the clipboard
     text that goes to it.  See remoteclip.c.  */
  struct rd_chunks clipboard;
  struct rd_chunk_queue clipboard_out;

  /* The font files that the client asked for and did not get, in
     order.  See remotefonts.c.  */
  struct remote_font_transfer *font_transfers;

  /* The images of the client.  remoteimage.c owns them.  */
  struct remote_images *images;

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
extern bool remote_frame_has_client (struct frame *);
extern bool remote_file_hash (Lisp_Object, unsigned char *, uint32_t *);
extern void remote_send_message (struct remote_display_info *,
				 const struct rd_message *);
extern ptrdiff_t remote_queued (struct remote_display_info *);
extern void remote_flush (struct remote_display_info *);
extern void remote_send_paced (struct remote_display_info *,
			       bool (*) (struct remote_display_info *));
extern Lisp_Object remote_font_file (const unsigned char *, uint32_t *);
extern void remote_forget_fonts (struct remote_display_info *,
				 const unsigned char *);
extern void remote_redraw_frames (struct remote_display_info *);
extern void remote_output_drained (struct remote_display_info *);
/* Fill the rectangle at X, Y with the size WIDTH, HEIGHT of the frame F
   with COLOR.  An empty rectangle sends nothing.  */
extern void remote_fill_area (struct frame *f, int x, int y, int width,
			      int height, unsigned long color);
extern void syms_of_remoteterm (void);

/* Defined in remoteproc.c.  */
extern void syms_of_remoteproc (void);

/* Defined in remoteclip.c.  */
extern void remote_clipboard_input (struct remote_display_info *,
				    const struct rd_message *);
extern void remote_drop_clipboard (struct remote_display_info *);
extern void remote_send_clipboard_data (struct remote_display_info *);
extern void syms_of_remoteclip (void);

/* Defined in remotefonts.c.  */
extern void remote_want_font (struct remote_display_info *,
			      const struct rd_message *);
extern void remote_send_font_data (struct remote_display_info *);
extern void remote_drop_font_transfers (struct remote_display_info *);

/* Defined in remoteimage.c.  */
extern uint32_t remote_image_prepare (struct frame *, struct image *);
extern void remote_send_images (struct remote_display_info *);
extern void remote_send_image_forgets (struct remote_display_info *);
extern void remote_init_images (struct remote_display_info *);
extern void remote_reset_images (struct remote_display_info *);
extern void remote_free_images (struct remote_display_info *);
extern void remote_set_last_image_id (uint32_t);
extern size_t remote_set_max_image_size (size_t);

/* In remoteterm.c.  */
extern struct frame *remote_frame_of_id (struct remote_display_info *,
					 uint32_t);
extern void remote_new_focus_frame (struct remote_display_info *,
				    struct frame *);

/* Defined in remotetest.c.  */
extern void syms_of_remotetest (void);

#endif /* EMACS_REMOTETERM_H */
