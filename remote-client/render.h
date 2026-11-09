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

#include <stdint.h>

#include <cairo.h>

#include "rdproto.h"

/* The largest width and height of a frame.  This is the limit of a
   Cairo image surface.  A define-frame message with a larger size
   deletes the frame.  */
enum { RC_MAX_FRAME_SIZE = 32767 };

struct rc_renderer;

extern struct rc_renderer *rc_renderer_new (void);
extern void rc_renderer_free (struct rc_renderer *renderer);

/* Apply MESSAGE to the frames of RENDERER.  A message for a frame that
   the server did not define changes nothing.  */
extern void rc_apply (struct rc_renderer *renderer,
		      const struct rd_message *message);

/* Return the surface of the frame FRAME of RENDERER, or NULL if the
   server did not define the frame.  The surface has the format
   CAIRO_FORMAT_RGB24.  It stays valid until the next call of
   rc_apply.  */
extern cairo_surface_t *rc_frame_surface (struct rc_renderer *renderer,
					  uint32_t frame);

#endif /* REMOTE_CLIENT_RENDER_H */
