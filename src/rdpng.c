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

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rdpng.h"

/* The bytes that the encoder wrote: SIZE bytes in BYTES, which has
   room for CAPACITY bytes.  */
struct sink
{
  unsigned char *bytes;
  size_t size;
  size_t capacity;
};

/* The bytes that the decoder reads: SIZE bytes in BYTES, of which it
   read USED.  */
struct source
{
  const unsigned char *bytes;
  size_t size;
  size_t used;
};

/* The first bytes of a PNG file: the signature, and the length and the
   name of the header chunk IHDR.  The width and the height of the
   image follow them, as 32-bit big-endian numbers.  */
static const unsigned char header[] =
  { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
    0, 0, 0, 13, 'I', 'H', 'D', 'R' };

static bool within_side_limit_p (const unsigned char *, size_t);
static uint32_t big_endian_at (const unsigned char *);
static cairo_status_t write_bytes (void *, const unsigned char *,
				   unsigned int);
static cairo_status_t read_bytes (void *, unsigned char *, unsigned int);

unsigned char *
rd_png_encode (cairo_surface_t *surface, size_t *size)
{
  struct sink sink = { NULL, 0, 0 };
  if (cairo_surface_write_to_png_stream (surface, write_bytes, &sink)
      != CAIRO_STATUS_SUCCESS)
    {
      free (sink.bytes);
      return NULL;
    }
  *size = sink.size;
  return sink.bytes;
}

cairo_surface_t *
rd_png_decode (const unsigned char *bytes, size_t size)
{
  if (!within_side_limit_p (bytes, size))
    return NULL;
  struct source source = { bytes, size, 0 };
  cairo_surface_t *surface
    = cairo_image_surface_create_from_png_stream (read_bytes, &source);
  if (cairo_surface_status (surface) == CAIRO_STATUS_SUCCESS)
    return surface;
  cairo_surface_destroy (surface);
  return NULL;
}

/* Return true if the SIZE bytes BYTES start with a PNG header, and the
   image has at most RD_MAX_IMAGE_SIDE pixels on each side.  The decoder
   checks this before it makes a surface, so a small file cannot make
   it allocate a very large surface.  */
static bool
within_side_limit_p (const unsigned char *bytes, size_t size)
{
  return (size >= sizeof header + 8
	  && memcmp (bytes, header, sizeof header) == 0
	  && big_endian_at (bytes + sizeof header) <= RD_MAX_IMAGE_SIDE
	  && big_endian_at (bytes + sizeof header + 4) <= RD_MAX_IMAGE_SIDE);
}

/* Return the 32-bit big-endian number at BYTES.  */
static uint32_t
big_endian_at (const unsigned char *bytes)
{
  return ((uint32_t) bytes[0] << 24 | (uint32_t) bytes[1] << 16
	  | (uint32_t) bytes[2] << 8 | bytes[3]);
}

/* Add the LENGTH bytes DATA to the sink CLOSURE.  */
static cairo_status_t
write_bytes (void *closure, const unsigned char *data, unsigned int length)
{
  struct sink *sink = closure;
  if (sink->capacity - sink->size < length)
    {
      size_t capacity = 2 * (sink->size + length);
      unsigned char *bytes = realloc (sink->bytes, capacity);
      if (!bytes)
	return CAIRO_STATUS_NO_MEMORY;
      sink->bytes = bytes;
      sink->capacity = capacity;
    }
  memcpy (sink->bytes + sink->size, data, length);
  sink->size += length;
  return CAIRO_STATUS_SUCCESS;
}

/* Copy the next LENGTH bytes of the source CLOSURE to DATA.  Fail if
   the source has fewer bytes.  */
static cairo_status_t
read_bytes (void *closure, unsigned char *data, unsigned int length)
{
  struct source *source = closure;
  if (source->size - source->used < length)
    return CAIRO_STATUS_READ_ERROR;
  memcpy (data, source->bytes + source->used, length);
  source->used += length;
  return CAIRO_STATUS_SUCCESS;
}
