/* Test clients of the remote display backend.

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

/* The tests of the remote display backend run in Emacs, with no real
   client.  This file gives them the clients and the functions that they
   need.  It uses only the interface of remoteterm.h, as a transport
   does.  A build has this file only if it is configured with
   --enable-remote-display-tests, because a Lisp call of these functions
   would replace the client of a session.  */

#include <config.h>

/* The tests read the statistics of malloc, if the C library has them.  */
#if (defined __GLIBC__						\
     && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)))
# define REMOTE_HAVE_MALLINFO2 1
# include <malloc.h>
#endif

#include "lisp.h"
#include "dispextern.h"
#include "frame.h"
#include "keyboard.h"
#include "termhooks.h"
#include "window.h"
#include "font.h"
#include "remoteinput.h"
#include "remoteterm.h"
#include "rdproto.h"
#include "rdpng.h"

static struct frame *remote_decode_frame (Lisp_Object);
static Lisp_Object remote_frame_or_nil (struct frame *);
static int remote_int (Lisp_Object);
static int remote_half_int (Lisp_Object);
static struct remote_sink *remote_make_recording_sink (void);
static void remote_recording_write (void *, const unsigned char *, ptrdiff_t);
static void remote_recording_flush (void *);
static void remote_recording_free (void *);
static struct remote_sink *remote_make_counting_sink (void);
static void remote_counting_write (void *, const unsigned char *, ptrdiff_t);
static void remote_counting_flush (void *);
static void remote_counting_free (void *);
static ptrdiff_t remote_recording_queued (void *);
static ptrdiff_t remote_counting_queued (void *);
static Lisp_Object remote_surface_pixels (cairo_surface_t *);

static const struct remote_sink_ops remote_recording_ops =
  { remote_recording_write, remote_recording_flush, remote_recording_free,
    remote_recording_queued };

static const struct remote_sink_ops remote_counting_ops =
  { remote_counting_write, remote_counting_flush, remote_counting_free,
    remote_counting_queued };

/* The tests attach a recording client to a session, and take the
   bytes that the session sends.  A counting client is a second kind
   of sink: it shows that a session needs only the sink interface.  */

/* The data of a recording sink: the encoded messages that the client
   has not taken, the number of bytes since the last flush, the number
   of bytes that the sink says it did not send, and the number of
   flushes that had bytes to send.  If COUNT_WRITES, the sink also says
   that it did not send the bytes since the last flush, as a transport
   does.  */
struct remote_recording
{
  unsigned char *output;
  ptrdiff_t used;
  ptrdiff_t size;
  ptrdiff_t unflushed;
  ptrdiff_t queued;
  ptrdiff_t flushes;
  bool count_writes;
};

/* The number of recording and counting sinks that exist.  */
static ptrdiff_t remote_live_test_sinks;

DEFUN ("remote-display--record", Fremote_display__record,
       Sremote_display__record, 1, 1, 0,
       doc: /* Attach a new recording client to the session DISPLAY.
Discard the bytes that the last client did not take.  The session
keeps the bytes that it sends, until a call of
`remote-display--take-output' takes them.  */)
  (Lisp_Object display)
{
  remote_set_sink (check_x_display_info (display),
		   remote_make_recording_sink ());
  return Qnil;
}

DEFUN ("remote-display--count", Fremote_display__count,
       Sremote_display__count, 1, 1, 0,
       doc: /* Attach a new counting client to the session DISPLAY.
The client keeps only the number of bytes that the session sends.  */)
  (Lisp_Object display)
{
  remote_set_sink (check_x_display_info (display),
		   remote_make_counting_sink ());
  return Qnil;
}

DEFUN ("remote-display--detach", Fremote_display__detach,
       Sremote_display__detach, 1, 1, 0,
       doc: /* Detach the client of the session DISPLAY, if it has one.  */)
  (Lisp_Object display)
{
  remote_set_sink (check_x_display_info (display), NULL);
  return Qnil;
}

DEFUN ("remote-display--take-output", Fremote_display__take_output,
       Sremote_display__take_output, 1, 1, 0,
       doc: /* Return and remove the bytes that the session DISPLAY sent.
The value is a unibyte string of encoded messages.  Signal an error if
the session has no recording client.  */)
  (Lisp_Object display)
{
  struct remote_recording *recording
    = remote_sink_data (check_x_display_info (display), &remote_recording_ops);
  if (!recording)
    error ("The session has no recording client");
  Lisp_Object bytes = make_unibyte_string ((char *) recording->output,
					   recording->used);
  recording->used = 0;
  return bytes;
}

DEFUN ("remote-display--set-queued", Fremote_display__set_queued,
       Sremote_display__set_queued, 2, 3, 0,
       doc: /* Make the recording client of DISPLAY report QUEUED bytes.
These are the bytes that a transport did not send yet.  The recording
client keeps all bytes, so the number is only what the session sees.
If COUNT-WRITES is non-nil, the client also reports the bytes since
the last flush, as a transport does.  Signal an error if the session
has no recording client.  */)
  (Lisp_Object display, Lisp_Object queued, Lisp_Object count_writes)
{
  struct remote_recording *recording
    = remote_sink_data (check_x_display_info (display), &remote_recording_ops);
  if (!recording)
    error ("The session has no recording client");
  recording->queued = check_integer_range (queued, 0, PTRDIFF_MAX);
  recording->count_writes = !NILP (count_writes);
  return Qnil;
}

DEFUN ("remote-display--input", Fremote_display__input,
       Sremote_display__input, 2, 2, 0,
       doc: /* Give the encoded message BYTES to DISPLAY as input of its client.
BYTES is a unibyte string with one message.  */)
  (Lisp_Object display, Lisp_Object bytes)
{
  CHECK_STRING (bytes);
  struct rd_message message;
  size_t used;
  if (rd_decode (SDATA (bytes), SBYTES (bytes), &message, &used) != RD_OK)
    error ("Not a message");
  remote_handle_input (check_x_display_info (display), &message);
  return Qnil;
}

DEFUN ("remote-display--queue-empty", Fremote_display__queue_empty,
       Sremote_display__queue_empty, 1, 1, 0,
       doc: /* Tell DISPLAY that its transport sent all its bytes.
A transport does this when its queue becomes empty.  */)
  (Lisp_Object display)
{
  remote_output_drained (check_x_display_info (display));
  return Qnil;
}

DEFUN ("remote-display--unflushed-bytes", Fremote_display__unflushed_bytes,
       Sremote_display__unflushed_bytes, 1, 1, 0,
       doc: /* Return the number of bytes since the last flush of DISPLAY.
Signal an error if the session has no recording client.  */)
  (Lisp_Object display)
{
  struct remote_recording *recording
    = remote_sink_data (check_x_display_info (display), &remote_recording_ops);
  if (!recording)
    error ("The session has no recording client");
  return make_int (recording->unflushed);
}

DEFUN ("remote-display--flushes", Fremote_display__flushes,
       Sremote_display__flushes, 1, 1, 0,
       doc: /* Return the number of flushes of DISPLAY that had bytes to send.
Signal an error if the session has no recording client.  */)
  (Lisp_Object display)
{
  struct remote_recording *recording
    = remote_sink_data (check_x_display_info (display), &remote_recording_ops);
  if (!recording)
    error ("The session has no recording client");
  return make_int (recording->flushes);
}

DEFUN ("remote-display--flush-display", Fremote_display__flush_display,
       Sremote_display__flush_display, 1, 1, 0,
       doc: /* Call the flush hook of the redisplay interface of FRAME.  */)
  (Lisp_Object frame)
{
  struct frame *f = remote_decode_frame (frame);
  FRAME_RIF (f)->flush_display (f);
  return Qnil;
}

DEFUN ("remote-display--counted-bytes", Fremote_display__counted_bytes,
       Sremote_display__counted_bytes, 1, 1, 0,
       doc: /* Return the number of bytes that a counting client received.
Return nil if the client of the session DISPLAY is not a counting
client.  */)
  (Lisp_Object display)
{
  ptrdiff_t *count = remote_sink_data (check_x_display_info (display),
				       &remote_counting_ops);
  return count ? make_int (*count) : Qnil;
}

DEFUN ("remote-display--kboard-keymap", Fremote_display__kboard_keymap,
       Sremote_display__kboard_keymap, 1, 1, 0,
       doc: /* Return a Lisp object that only the kboard of DISPLAY holds.
The garbage collector frees the object after the kboard goes away, so
the tests use it to find a kboard that nothing freed.  */)
  (Lisp_Object display)
{
  struct terminal *t = check_x_display_info (display)->terminal;
  return KVAR (t->kboard, Vlocal_function_key_map);
}

DEFUN ("remote-display--live-sinks", Fremote_display__live_sinks,
       Sremote_display__live_sinks, 0, 0, 0,
       doc: /* Return the number of recording and counting sinks that exist.
The tests use this to find a sink that nothing freed.  */)
  (void)
{
  return make_int (remote_live_test_sinks);
}

DEFUN ("remote-display--malloc-used", Fremote_display__malloc_used,
       Sremote_display__malloc_used, 0, 0, 0,
       doc: /* Return the number of bytes that malloc holds for this process.
Return nil if the C library does not give this number.  The tests use
this to find a leak that is too small for the size of the process to
show.  */)
  (void)
{
#ifdef REMOTE_HAVE_MALLINFO2
  return make_uint (mallinfo2 ().uordblks);
#else
  return Qnil;
#endif
}

DEFUN ("remote-display--focus", Fremote_display__focus,
       Sremote_display__focus, 1, 1, 0,
       doc: /* Give FRAME the focus of its session.
The selected window of a focused frame shows the active cursor.  */)
  (Lisp_Object frame)
{
  struct frame *f = remote_decode_frame (frame);
  remote_new_focus_frame (FRAME_DISPLAY_INFO (f), f);
  return Qnil;
}

DEFUN ("remote-display--focus-frames", Fremote_display__focus_frames,
       Sremote_display__focus_frames, 1, 1, 0,
       doc: /* Return the focus frame and the highlight frame of DISPLAY.
The value is a list of two elements.  An element is nil if the session
has no such frame.  */)
  (Lisp_Object display)
{
  struct remote_display_info *dpyinfo = check_x_display_info (display);
  return list2 (remote_frame_or_nil (dpyinfo->focus_frame),
		remote_frame_or_nil (dpyinfo->highlight_frame));
}

DEFUN ("remote-display--mouse-frame", Fremote_display__mouse_frame,
       Sremote_display__mouse_frame, 1, 1, 0,
       doc: /* Return the frame of the mouse highlight of DISPLAY, or nil.  */)
  (Lisp_Object display)
{
  struct remote_display_info *dpyinfo = check_x_display_info (display);
  return remote_frame_or_nil (dpyinfo->mouse_highlight.mouse_face_mouse_frame);
}

DEFUN ("remote-display--mouse-face-window",
       Fremote_display__mouse_face_window,
       Sremote_display__mouse_face_window, 1, 1, 0,
       doc: /* Return the window with the mouse highlight of DISPLAY.
Return nil if no window has the mouse highlight.  */)
  (Lisp_Object display)
{
  struct remote_display_info *dpyinfo = check_x_display_info (display);
  return dpyinfo->mouse_highlight.mouse_face_window;
}

DEFUN ("remote-display--clear-area", Fremote_display__clear_area,
       Sremote_display__clear_area, 5, 5, 0,
       doc: /* Call the clear hook of FRAME with X, Y, WIDTH, and HEIGHT.
The tests use this to give the hook values that redisplay seldom gives.
Each value is an integer in the range of the C type int.  */)
  (Lisp_Object frame, Lisp_Object x, Lisp_Object y,
   Lisp_Object width, Lisp_Object height)
{
  struct frame *f = remote_decode_frame (frame);
  FRAME_RIF (f)->clear_frame_area (f, remote_int (x), remote_int (y),
				   remote_int (width), remote_int (height));
  return Qnil;
}

DEFUN ("remote-display--scroll-run", Fremote_display__scroll_run,
       Sremote_display__scroll_run, 4, 4, 0,
       doc: /* Call the scroll hook of WINDOW for a run of pixel rows.
The run moves HEIGHT rows from CURRENT-Y to DESIRED-Y.  The tests use
this to give the hook values that redisplay seldom gives.  Each value
is an integer in the range of the C type int.  */)
  (Lisp_Object window, Lisp_Object current_y, Lisp_Object desired_y,
   Lisp_Object height)
{
  struct window *w = decode_live_window (window);
  struct frame *f = remote_decode_frame (w->frame);
  struct run run = { .current_y = remote_int (current_y),
		     .desired_y = remote_int (desired_y),
		     .height = remote_int (height) };
  FRAME_RIF (f)->scroll_run_hook (w, &run);
  return Qnil;
}

DEFUN ("remote-display--draw-fringe", Fremote_display__draw_fringe,
       Sremote_display__draw_fringe, 7, 7, 0,
       doc: /* Call the fringe hook of WINDOW for a bitmap at the frame row Y.
The value of row I of the data of the bitmap is I.  The bitmap has the
size WIDTH, HEIGHT, and starts at row OFFSET of its data.  Its line
starts at ROW-Y of WINDOW and has VISIBLE-HEIGHT visible rows.  The
tests use this to give the hook values that redisplay seldom gives.
WIDTH is an integer in the range of the C type int, HEIGHT is at most
two times the rows of a message, and OFFSET has a magnitude of at most
the rows of a message.  Y, ROW-Y, and VISIBLE-HEIGHT have a magnitude
of at most half of the largest int.  */)
  (Lisp_Object window, Lisp_Object width, Lisp_Object height,
   Lisp_Object offset, Lisp_Object y, Lisp_Object row_y,
   Lisp_Object visible_height)
{
  struct window *w = decode_live_window (window);
  struct frame *f = remote_decode_frame (w->frame);

  /* The hook reads no row after the row at the offset plus the
     height.  */
  static unsigned short bits[3 * RD_MAX_FRINGE_ROWS];
  for (int i = 0; i < countof (bits); i++)
    bits[i] = i;
  struct glyph_row row =
    {
      .y = remote_half_int (row_y),
      .visible_height = remote_half_int (visible_height),
    };
  struct draw_fringe_bitmap_params p =
    {
      .which = 1, .bits = bits, .bx = -1, .nx = 1, .ny = 1,
      .wd = remote_int (width), .bitmap_wd = remote_int (width),
      .h = check_integer_range (height, INT_MIN, 2 * RD_MAX_FRINGE_ROWS),
      .dh = check_integer_range (offset, -RD_MAX_FRINGE_ROWS,
				 RD_MAX_FRINGE_ROWS),
      .y = remote_half_int (y),
      .face = FACE_FROM_ID (f, DEFAULT_FACE_ID),
    };
  FRAME_RIF (f)->draw_fringe_bitmap (w, &row, &p);
  return Qnil;
}

DEFUN ("remote-display--draw-cursor", Fremote_display__draw_cursor,
       Sremote_display__draw_cursor, 2, 2, 0,
       doc: /* Call the cursor hook of WINDOW for a box at column HPOS.
The cursor is on the first row of WINDOW.  The tests use this to give
the hook a column that has no glyph.  The state of the cursor of WINDOW
does not change.  HPOS is an integer in the range of the C type int.  */)
  (Lisp_Object window, Lisp_Object hpos)
{
  struct window *w = decode_live_window (window);
  struct frame *f = remote_decode_frame (w->frame);
  struct window saved = *w;
  w->phys_cursor.hpos = remote_int (hpos);
  w->phys_cursor.vpos = 0;
  FRAME_RIF (f)->draw_window_cursor (w, MATRIX_ROW (w->current_matrix, 0),
				     0, 0, FILLED_BOX_CURSOR, 1, true, true);
  w->phys_cursor = saved.phys_cursor;
  w->phys_cursor_type = saved.phys_cursor_type;
  w->phys_cursor_width = saved.phys_cursor_width;
  w->phys_cursor_on_p = saved.phys_cursor_on_p;
  return Qnil;
}

DEFUN ("remote-display--draw-overlap", Fremote_display__draw_overlap,
       Sremote_display__draw_overlap, 1, 1, 0,
       doc: /* Draw the first row of WINDOW again, for its neighbor rows.
Each glyph of the row counts as a glyph that goes over them.  The tests
use this because a glyph of the fonts of a test rarely goes over its
row.  */)
  (Lisp_Object window)
{
  struct window *w = decode_live_window (window);
  struct frame *f = remote_decode_frame (w->frame);
  struct glyph_row *row = MATRIX_ROW (w->current_matrix, 0);
  for (int i = 0; i < row->used[TEXT_AREA]; i++)
    row->glyphs[TEXT_AREA][i].overlaps_vertically_p = true;
  FRAME_RIF (f)->fix_overlapping_area (w, row, TEXT_AREA, OVERLAPS_BOTH);
  return Qnil;
}

DEFUN ("remote-display--cursor-state", Fremote_display__cursor_state,
       Sremote_display__cursor_state, 1, 1, 0,
       doc: /* Return the state of the cursor that WINDOW keeps for redisplay.
The value is a list of the column, the row, the type, the width, and
whether the cursor is on.  */)
  (Lisp_Object window)
{
  struct window *w = decode_live_window (window);
  return list5 (make_int (w->phys_cursor.hpos), make_int (w->phys_cursor.vpos),
		make_int (w->phys_cursor_type), make_int (w->phys_cursor_width),
		w->phys_cursor_on_p ? Qt : Qnil);
}

DEFUN ("remote-display--file-hash", Fremote_display__file_hash,
       Sremote_display__file_hash, 1, 1, 0,
       doc: /* Return the hash that a define-font message gives for FILE.
The value is a vector of bytes, and it is empty if FILE cannot be
read or is not a string.  */)
  (Lisp_Object file)
{
  unsigned char hash[RD_HASH_SIZE];
  uint32_t size;
  if (!remote_file_hash (file, hash, &size))
    return make_nil_vector (0);
  Lisp_Object bytes = make_nil_vector (RD_HASH_SIZE);
  for (int i = 0; i < RD_HASH_SIZE; i++)
    ASET (bytes, i, make_fixnum (hash[i]));
  return bytes;
}

DEFUN ("remote-display--set-font-file", Fremote_display__set_font_file,
       Sremote_display__set_font_file, 2, 2, 0,
       doc: /* Set the file name of the font object FONT to FILE.
Return FILE.  The tests use this to get a font whose file the server
cannot read.  */)
  (Lisp_Object font, Lisp_Object file)
{
  CHECK_FONT_OBJECT (font);
  ASET (font, FONT_FILE_INDEX, file);
  return file;
}

DEFUN ("remote-display--set-last-frame-id", Fremote_display__set_last_frame_id,
       Sremote_display__set_last_frame_id, 2, 2, 0,
       doc: /* Set the frame ID counter of the session DISPLAY to ID.
Return the value that the counter holds.  The tests use this to reach
the end of the range of frame IDs.  */)
  (Lisp_Object display, Lisp_Object id)
{
  struct remote_display_info *dpyinfo = check_x_display_info (display);
  dpyinfo->last_frame_id = check_integer_range (id, 0, UINT32_MAX);
  return make_uint (dpyinfo->last_frame_id);
}

DEFUN ("remote-display--set-last-image-id",
       Fremote_display__set_last_image_id,
       Sremote_display__set_last_image_id, 1, 1, 0,
       doc: /* Set the counter of image IDs to ID, and return ID.
The tests use this to reach the end of the range of image IDs.  */)
  (Lisp_Object id)
{
  remote_set_last_image_id (check_integer_range (id, 0, UINT32_MAX));
  return id;
}

DEFUN ("remote-display--set-max-image-size",
       Fremote_display__set_max_image_size,
       Sremote_display__set_max_image_size, 1, 1, 0,
       doc: /* Set the largest PNG file of an image to SIZE bytes.
Return the limit before the call.  The tests use this to get an image
whose PNG file is too large, with no large image.  */)
  (Lisp_Object size)
{
  return make_uint (remote_set_max_image_size
		    (check_integer_range (size, 0, RD_MAX_IMAGE_SIZE)));
}

DEFUN ("remote-display--png-pixels", Fremote_display__png_pixels,
       Sremote_display__png_pixels, 1, 1, 0,
       doc: /* Decode the PNG FILE, a unibyte string, as a client does.
Return (WIDTH HEIGHT . PIXELS), where PIXELS is a vector of the
premultiplied ARGB32 pixels, row after row.  Return nil if FILE is not
a PNG file that the client accepts.  */)
  (Lisp_Object file)
{
  CHECK_STRING (file);
  cairo_surface_t *surface = rd_png_decode (SDATA (file), SBYTES (file));
  if (!surface)
    return Qnil;
  Lisp_Object pixels = remote_surface_pixels (surface);
  Lisp_Object size
    = Fcons (make_int (cairo_image_surface_get_width (surface)),
	     Fcons (make_int (cairo_image_surface_get_height (surface)),
		    pixels));
  cairo_surface_destroy (surface);
  return size;
}

/* Return a vector of the ARGB32 pixels of the image surface SURFACE,
   row after row.  */
static Lisp_Object
remote_surface_pixels (cairo_surface_t *surface)
{
  int width = cairo_image_surface_get_width (surface);
  int height = cairo_image_surface_get_height (surface);
  int stride = cairo_image_surface_get_stride (surface);
  unsigned char *data = cairo_image_surface_get_data (surface);
  Lisp_Object pixels = make_nil_vector (width * height);
  for (int y = 0; y < height; y++)
    {
      uint32_t *row = (uint32_t *) (data + y * stride);
      for (int x = 0; x < width; x++)
	ASET (pixels, y * width + x, make_uint (row[x]));
    }
  return pixels;
}

/* Return the frame of FRAME.  Signal an error if it is not a live
   remote frame.  The hooks that the test functions call are only for
   remote frames.  */
static struct frame *
remote_decode_frame (Lisp_Object frame)
{
  struct frame *f = decode_live_frame (frame);
  if (!FRAME_REMOTE_P (f))
    error ("The frame is not a remote frame");
  return f;
}

/* Return the Lisp object of F, or nil if F is NULL.  */
static Lisp_Object
remote_frame_or_nil (struct frame *f)
{
  Lisp_Object frame = Qnil;
  if (f)
    XSETFRAME (frame, f);
  return frame;
}

/* Return the value of VALUE, an integer in the range of int.  */
static int
remote_int (Lisp_Object value)
{
  return check_integer_range (value, INT_MIN, INT_MAX);
}

/* Return the value of VALUE, an integer that has a magnitude of at
   most half of the largest int.  A sum of two such values fits in an
   int.  */
static int
remote_half_int (Lisp_Object value)
{
  return check_integer_range (value, INT_MIN / 2, INT_MAX / 2);
}

/* Return a new sink that keeps the bytes for
   `remote-display--take-output'.  */
static struct remote_sink *
remote_make_recording_sink (void)
{
  remote_live_test_sinks++;
  return remote_make_sink (&remote_recording_ops,
			   xzalloc (sizeof (struct remote_recording)));
}

/* The write function of a recording sink.  */
static void
remote_recording_write (void *data, const unsigned char *bytes, ptrdiff_t size)
{
  struct remote_recording *recording = data;
  ptrdiff_t needed = recording->used + size - recording->size;
  if (needed > 0)
    recording->output = xpalloc (recording->output, &recording->size,
				 needed, -1, 1);
  memcpy (recording->output + recording->used, bytes, size);
  recording->used += size;
  recording->unflushed += size;
}

/* The flush function of a recording sink.  */
static void
remote_recording_flush (void *data)
{
  struct remote_recording *recording = data;
  if (recording->unflushed > 0)
    recording->flushes++;
  recording->unflushed = 0;
}

/* The free function of a recording sink.  */
static void
remote_recording_free (void *data)
{
  struct remote_recording *recording = data;
  xfree (recording->output);
  xfree (recording);
  remote_live_test_sinks--;
}

/* Return a new sink that keeps only the number of bytes.  */
static struct remote_sink *
remote_make_counting_sink (void)
{
  remote_live_test_sinks++;
  return remote_make_sink (&remote_counting_ops,
			   xzalloc (sizeof (ptrdiff_t)));
}

/* The write function of a counting sink.  */
static void
remote_counting_write (void *data, const unsigned char *bytes, ptrdiff_t size)
{
  ptrdiff_t *count = data;
  *count += size;
}

/* The flush function of a counting sink.  It keeps no bytes.  */
static void
remote_counting_flush (void *data)
{
}

/* The free function of a counting sink.  */
static void
remote_counting_free (void *data)
{
  xfree (data);
  remote_live_test_sinks--;
}

/* The queued function of a recording sink.  It gives the number that
   the test set.  */
static ptrdiff_t
remote_recording_queued (void *data)
{
  struct remote_recording *recording = data;
  return (recording->queued
	  + (recording->count_writes ? recording->unflushed : 0));
}

/* The queued function of a counting sink.  It sends each byte at
   once.  */
static ptrdiff_t
remote_counting_queued (void *data)
{
  return 0;
}

void
syms_of_remotetest (void)
{
  DEFSYM (Qremote_test, "remote-test");

  defsubr (&Sremote_display__record);
  defsubr (&Sremote_display__file_hash);
  defsubr (&Sremote_display__set_font_file);
  defsubr (&Sremote_display__count);
  defsubr (&Sremote_display__detach);
  defsubr (&Sremote_display__take_output);
  defsubr (&Sremote_display__unflushed_bytes);
  defsubr (&Sremote_display__flushes);
  defsubr (&Sremote_display__flush_display);
  defsubr (&Sremote_display__counted_bytes);
  defsubr (&Sremote_display__kboard_keymap);
  defsubr (&Sremote_display__live_sinks);
  defsubr (&Sremote_display__malloc_used);
  defsubr (&Sremote_display__focus_frames);
  defsubr (&Sremote_display__focus);
  defsubr (&Sremote_display__mouse_frame);
  defsubr (&Sremote_display__mouse_face_window);
  defsubr (&Sremote_display__clear_area);
  defsubr (&Sremote_display__scroll_run);
  defsubr (&Sremote_display__draw_fringe);
  defsubr (&Sremote_display__draw_cursor);
  defsubr (&Sremote_display__draw_overlap);
  defsubr (&Sremote_display__cursor_state);
  defsubr (&Sremote_display__set_last_frame_id);
  defsubr (&Sremote_display__set_last_image_id);
  defsubr (&Sremote_display__set_max_image_size);
  defsubr (&Sremote_display__png_pixels);
  defsubr (&Sremote_display__set_queued);
  defsubr (&Sremote_display__input);
  defsubr (&Sremote_display__queue_empty);

  Fprovide (Qremote_test, Qnil);
}
