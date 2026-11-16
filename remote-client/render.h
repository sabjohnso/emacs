/* The render module of the remote display client.

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

/* A renderer keeps one Cairo image surface for each frame that the
   server defined, and applies each message of the server to these
   surfaces.  The module does not use GTK, so a test can use it with
   no display.  */

#ifndef REMOTE_CLIENT_RENDER_H
#define REMOTE_CLIENT_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include <cairo.h>

#include "rdproto.h"

/* The largest width and height of a frame.  This is the limit of a
   Cairo image surface.  A define-frame message with a larger size
   deletes the frame.  */
enum { RC_MAX_FRAME_SIZE = 32767 };

/* The largest size of a font in pixels.  A define-font message with a
   larger size defines no font.  Cairo puts the face of a font of 65536
   pixels or more into an error that stops each font of the face.  A
   glyph of RC_MAX_FONT_SIZE pixels needs a glyph image of 16 MB.  */
enum { RC_MAX_FONT_SIZE = 4096 };

/* The font of a define-font message: the face INDEX of the font file
   whose SHA-256 hash is the NHASH bytes HASH.  The file has FILE_SIZE
   bytes.  */
struct rc_font_key
{
  const uint8_t *hash;
  int nhash;
  uint32_t index;
  uint32_t file_size;
};

/* A source of the fonts that a renderer draws with.  FIND returns a
   new reference to the Cairo font face of the font KEY.  It returns
   NULL if it has no face for KEY.  DATA is the first argument of FIND
   and DESTROY.  The maker of a source owns it, and frees it with
   rc_font_source_free.  DESTROY frees DATA, and can be NULL.  */
struct rc_font_source
{
  cairo_font_face_t *(*find) (void *data, const struct rc_font_key *key);
  void (*destroy) (void *data);
  void *data;
};

/* Free the data of the font source SOURCE.  */
extern void rc_font_source_free (struct rc_font_source source);

struct rc_renderer;

/* Return a new renderer that gets its fonts from FONTS.  FONTS must
   stay valid until rc_renderer_free frees the renderer.  */
extern struct rc_renderer *rc_renderer_new (struct rc_font_source fonts);
extern void rc_renderer_free (struct rc_renderer *renderer);

/* Free the images of RENDERER.  The image IDs of the server are valid
   for one connection only, so the client calls this function for each
   new connection.  */
extern void rc_renderer_forget_images (struct rc_renderer *renderer);

/* Return true if the last define-font message for the font ID FONT
   gave RENDERER a face to draw with.  */
extern bool rc_renderer_has_font (const struct rc_renderer *renderer,
				  uint32_t font);

/* The kinds of change of a frame.  */
enum rc_change_kind
  {
    /* The message changed no frame.  */
    RC_NO_CHANGE,
    /* The frame exists after a define-frame message, with a new size
       or as a new frame.  */
    RC_FRAME_SHOWN,
    /* The frame does not exist after a define-frame or a delete-frame
       message.  */
    RC_FRAME_REMOVED,
    /* The message was a draw in the frame, so it can have changed
       pixels of the frame.  */
    RC_FRAME_DRAWN,
  };

/* The change of one message: its KIND, and the FRAME that it changed.
   FRAME has no value for RC_NO_CHANGE.  */
struct rc_change
{
  enum rc_change_kind kind;
  uint32_t frame;
};

/* Apply MESSAGE to the frames of RENDERER, and return its change.  A
   message for a frame that the server did not define changes nothing.
   A draw-glyphs message for a font that the server did not define, or
   that the font source did not find, changes no pixel.  A draw-image
   message for an image that RENDERER does not have changes nothing.  */
extern struct rc_change rc_apply (struct rc_renderer *renderer,
				  const struct rd_message *message);

/* Return the surface of the frame FRAME of RENDERER, or NULL if the
   server did not define the frame.  The surface has the format
   CAIRO_FORMAT_RGB24.  It stays valid until the next call of
   rc_apply.  */
extern cairo_surface_t *rc_frame_surface (struct rc_renderer *renderer,
					  uint32_t frame);

/* The functions of the frames for the stack (stack.h) and the
   composition (compose.h).  RENDERER is the struct rc_renderer.  */

/* Put the size of the surface of FRAME in WIDTH and HEIGHT, and return
   true.  Return false if the server did not define FRAME.  */
extern bool rc_frame_size (void *renderer, uint32_t frame, int *width,
			   int *height);

/* Return rc_frame_surface (RENDERER, FRAME).  */
extern cairo_surface_t *rc_frame_surface_of (void *renderer,
					     uint32_t frame);

#endif /* REMOTE_CLIENT_RENDER_H */
