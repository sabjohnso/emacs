/* Test the PNG encoder and decoder of the remote display.

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

/* The laws of ../src/rdpng.c, for random surfaces with 1 to MAX_SIDE
   pixels on each side.

   - Round trip with alpha: for an ARGB32 surface with random valid
     premultiplied pixels, decode (encode S) has the size and the
     pixels of S.  Each channel of a pixel is (C * A + 127) / 255 for
     a random straight color C and a random alpha A, so it is a valid
     premultiplied pixel.
   - Round trip with no alpha: for an RGB24 surface with random
     pixels, decode (encode S) has the size and the pixels of S.
   - Truncated: decode of the first bytes of a PNG file gives NULL.
     The law tries the file with no last byte, and TRUNCATIONS random
     sizes that are shorter than the file.
   - Random bytes: decode of random bytes, with or without the PNG
     signature first, gives NULL.
   - Damaged: decode of a PNG file with DAMAGES random bytes changed
     after its header gives NULL or a surface with the size in the
     header, and does not read outside the bytes.
   - Empty: decode of no bytes gives NULL.
   - Side limit: decode of a PNG with RD_MAX_IMAGE_SIDE + 1 pixels on
     one side gives NULL.  A PNG with RD_MAX_IMAGE_SIDE pixels on a
     side decodes.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cairo.h>

#include "rdpng.h"

enum { TRIALS = 100 };

/* The largest side of a random surface, in pixels.  */
enum { MAX_SIDE = 64 };

/* The number of random sizes that the truncated law tries for each
   file.  */
enum { TRUNCATIONS = 16 };

/* The number of bytes that the damaged law changes in each file.  */
enum { DAMAGES = 4 };

/* The number of bytes of a PNG header: the signature, and the IHDR
   chunk up to and with the width and the height.  */
enum { HEADER_SIZE = 24 };

/* The largest number of random bytes.  */
enum { MAX_BYTES = 4096 };

/* The bytes of a PNG file.  */
struct png
{
  unsigned char *bytes;
  size_t size;
};

static const char *run_trial (uint64_t);
static bool alpha_round_trip_law (uint64_t *);
static bool opaque_round_trip_law (uint64_t *);
static bool truncated_law (uint64_t *);
static bool random_bytes_law (uint64_t *);
static bool damaged_law (uint64_t *);
static bool empty_example (uint64_t *);
static bool side_limit_example (uint64_t *);
static bool round_trip (cairo_surface_t *);
static bool decodes_p (cairo_surface_t *);
static cairo_surface_t *random_argb_surface (uint64_t *);
static cairo_surface_t *random_rgb_surface (uint64_t *);
static cairo_surface_t *random_surface (uint64_t *, cairo_format_t);
static uint32_t random_premultiplied_pixel (uint64_t *);
static struct png encode (cairo_surface_t *);
static bool same_pixels_p (cairo_surface_t *, cairo_surface_t *);
static uint32_t pixel_at (cairo_surface_t *, int, int);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "round trip with alpha", alpha_round_trip_law },
    { "round trip with no alpha", opaque_round_trip_law },
    { "truncated", truncated_law },
    { "random bytes", random_bytes_law },
    { "damaged", damaged_law },
    { "empty", empty_example },
    { "side limit", side_limit_example },
  };

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 1 : 0;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    {
      const char *failed = run_trial (base + trial);
      if (failed)
	{
	  fprintf (stderr,
		   "test-rdpng: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-rdpng: %d trials passed\n", TRIALS);
  return 0;
}

/* Run each law with SEED.  Return the name of the first law that
   fails, or NULL.  */
static const char *
run_trial (uint64_t seed)
{
  for (size_t i = 0; i < sizeof laws / sizeof *laws; i++)
    {
      uint64_t state = seed;
      if (!laws[i].check (&state))
	return laws[i].name;
    }
  return NULL;
}

static bool
alpha_round_trip_law (uint64_t *state)
{
  return round_trip (random_argb_surface (state));
}

static bool
opaque_round_trip_law (uint64_t *state)
{
  return round_trip (random_rgb_surface (state));
}

static bool
truncated_law (uint64_t *state)
{
  cairo_surface_t *surface = random_argb_surface (state);
  struct png png = encode (surface);
  cairo_surface_destroy (surface);
  bool passed = png.bytes != NULL && png.size > 0;
  if (passed)
    passed = rd_png_decode (png.bytes, png.size - 1) == NULL;
  for (int i = 0; passed && i < TRUNCATIONS; i++)
    passed = rd_png_decode (png.bytes,
			    random_below (state, png.size)) == NULL;
  free (png.bytes);
  return passed;
}

static bool
random_bytes_law (uint64_t *state)
{
  static const unsigned char signature[] =
    { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
  unsigned char bytes[MAX_BYTES];
  size_t size = random_below (state, MAX_BYTES) + 1;
  for (size_t i = 0; i < size; i++)
    bytes[i] = random_next (state);
  bool passed = rd_png_decode (bytes, size) == NULL;
  if (size > sizeof signature)
    {
      memcpy (bytes, signature, sizeof signature);
      passed = passed && rd_png_decode (bytes, size) == NULL;
    }
  return passed;
}

static bool
damaged_law (uint64_t *state)
{
  cairo_surface_t *surface = random_argb_surface (state);
  int width = cairo_image_surface_get_width (surface);
  int height = cairo_image_surface_get_height (surface);
  struct png png = encode (surface);
  cairo_surface_destroy (surface);
  if (!png.bytes || png.size <= HEADER_SIZE)
    {
      free (png.bytes);
      return false;
    }
  for (int i = 0; i < DAMAGES; i++)
    png.bytes[HEADER_SIZE + random_below (state, png.size - HEADER_SIZE)]
      ^= random_below (state, 255) + 1;
  cairo_surface_t *decoded = rd_png_decode (png.bytes, png.size);
  free (png.bytes);
  if (!decoded)
    return true;
  bool passed = (cairo_image_surface_get_width (decoded) == width
		 && cairo_image_surface_get_height (decoded) == height);
  cairo_surface_destroy (decoded);
  return passed;
}

static bool
empty_example (uint64_t *state)
{
  (void) state;
  unsigned char byte = 0;
  return rd_png_decode (&byte, 0) == NULL;
}

static bool
side_limit_example (uint64_t *state)
{
  (void) state;
  cairo_format_t format = CAIRO_FORMAT_RGB24;
  return (decodes_p (cairo_image_surface_create (format,
						 RD_MAX_IMAGE_SIDE, 1))
	  && decodes_p (cairo_image_surface_create (format,
						    1, RD_MAX_IMAGE_SIDE))
	  && !decodes_p (cairo_image_surface_create (format,
						     RD_MAX_IMAGE_SIDE + 1,
						     1))
	  && !decodes_p (cairo_image_surface_create (format, 1,
						     RD_MAX_IMAGE_SIDE + 1)));
}

/* Return true if decode (encode SURFACE) has the size and the pixels
   of SURFACE.  Destroy SURFACE.  */
static bool
round_trip (cairo_surface_t *surface)
{
  struct png png = encode (surface);
  cairo_surface_t *decoded = (png.bytes
			      ? rd_png_decode (png.bytes, png.size) : NULL);
  bool passed = decoded && same_pixels_p (surface, decoded);
  if (decoded)
    cairo_surface_destroy (decoded);
  free (png.bytes);
  cairo_surface_destroy (surface);
  return passed;
}

/* Return true if decode (encode SURFACE) gives a surface.  Destroy
   SURFACE.  */
static bool
decodes_p (cairo_surface_t *surface)
{
  struct png png = encode (surface);
  cairo_surface_destroy (surface);
  if (!png.bytes)
    return false;
  cairo_surface_t *decoded = rd_png_decode (png.bytes, png.size);
  free (png.bytes);
  if (!decoded)
    return false;
  cairo_surface_destroy (decoded);
  return true;
}

/* Return a random ARGB32 surface with valid premultiplied pixels.  */
static cairo_surface_t *
random_argb_surface (uint64_t *state)
{
  return random_surface (state, CAIRO_FORMAT_ARGB32);
}

/* Return a random RGB24 surface.  */
static cairo_surface_t *
random_rgb_surface (uint64_t *state)
{
  return random_surface (state, CAIRO_FORMAT_RGB24);
}

/* Return a surface of FORMAT with a random size and random pixels.  */
static cairo_surface_t *
random_surface (uint64_t *state, cairo_format_t format)
{
  int width = random_below (state, MAX_SIDE) + 1;
  int height = random_below (state, MAX_SIDE) + 1;
  cairo_surface_t *surface
    = cairo_image_surface_create (format, width, height);
  cairo_surface_flush (surface);
  unsigned char *data = cairo_image_surface_get_data (surface);
  int stride = cairo_image_surface_get_stride (surface);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
	uint32_t pixel = random_premultiplied_pixel (state);
	if (format == CAIRO_FORMAT_RGB24)
	  pixel |= 0xff000000;
	memcpy (data + y * stride + x * 4, &pixel, sizeof pixel);
      }
  cairo_surface_mark_dirty (surface);
  return surface;
}

/* Return a random valid premultiplied ARGB32 pixel.  Some pixels are
   opaque, and some are transparent.  */
static uint32_t
random_premultiplied_pixel (uint64_t *state)
{
  uint64_t bits = random_next (state);
  uint32_t alpha;
  switch (random_below (state, 4))
    {
    case 0: alpha = 0; break;
    case 1: alpha = 255; break;
    default: alpha = bits >> 24 & 0xff; break;
    }
  uint32_t pixel = alpha << 24;
  for (int shift = 0; shift < 24; shift += 8)
    {
      uint32_t color = bits >> shift & 0xff;
      pixel |= (color * alpha + 127) / 255 << shift;
    }
  return pixel;
}

/* Return the PNG file of SURFACE, or no bytes if the encoder
   fails.  */
static struct png
encode (cairo_surface_t *surface)
{
  struct png png = { NULL, 0 };
  png.bytes = rd_png_encode (surface, &png.size);
  return png;
}

/* Return true if A and B have the same size and the same pixels.  */
static bool
same_pixels_p (cairo_surface_t *a, cairo_surface_t *b)
{
  int width = cairo_image_surface_get_width (a);
  int height = cairo_image_surface_get_height (a);
  if (width != cairo_image_surface_get_width (b)
      || height != cairo_image_surface_get_height (b))
    return false;
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      if (pixel_at (a, x, y) != pixel_at (b, x, y))
	return false;
  return true;
}

/* Return the ARGB32 pixel at X, Y of SURFACE.  A pixel of an RGB24
   surface is opaque.  */
static uint32_t
pixel_at (cairo_surface_t *surface, int x, int y)
{
  cairo_surface_flush (surface);
  const unsigned char *data = cairo_image_surface_get_data (surface);
  int stride = cairo_image_surface_get_stride (surface);
  uint32_t pixel;
  memcpy (&pixel, data + y * stride + x * 4, sizeof pixel);
  if (cairo_image_surface_get_format (surface) == CAIRO_FORMAT_RGB24)
    pixel |= 0xff000000;
  return pixel;
}

/* Return a random number below LIMIT.  */
static uint64_t
random_below (uint64_t *state, uint64_t limit)
{
  return random_next (state) % limit;
}

/* Return the next number of the generator splitmix64.  */
static uint64_t
random_next (uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}
