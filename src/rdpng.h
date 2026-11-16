/* Encode the pixels of an image as a PNG file, and decode them again.

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

/* The server (Emacs) sends the pixels of an image as a PNG file in
   image-data messages (rdproto.h).  The client decodes the file to a
   Cairo surface.  Both sides use this file, as they use rdproto.c, so
   both sides have the same rules.  The file uses no Lisp objects.  */

#ifndef EMACS_RDPNG_H
#define EMACS_RDPNG_H

#include <stddef.h>

#include <cairo.h>

#include "rdproto.h"

/* Return the bytes of a PNG file with the pixels of the image surface
   SURFACE, and set *SIZE to the number of bytes.  Return NULL if the
   encoder fails.  Free the bytes with free.  */
extern unsigned char *rd_png_encode (cairo_surface_t *surface,
				     size_t *size);

/* Return a new image surface with the pixels of the PNG file in the
   SIZE bytes BYTES.  Return NULL if the bytes are not a complete PNG
   file, or if the image has more than RD_MAX_IMAGE_SIDE pixels on a
   side.  Destroy the surface with cairo_surface_destroy.  */
extern cairo_surface_t *rd_png_decode (const unsigned char *bytes,
				       size_t size);

#endif /* EMACS_RDPNG_H */
