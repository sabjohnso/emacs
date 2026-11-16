/* Test the image store of the remote display client.

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

/* The laws of images.c, for random images with 1 to MAX_SIDE pixels
   on each side.  The store gets the PNG file of an image in
   image-data messages, and the test splits each file at random
   offsets.

   - Join: the image-data messages of the PNG file of a surface S give
     an image with the size and the pixels of S.  Before the last
     message, the store has no image for the ID.
   - Forget: after forget-image of an ID, the store has no image for
     the ID, and keeps the other images.  The forget of an ID that the
     store does not have changes nothing.
   - Clear: after a clear, the store has no images.  The messages of
     an image that was not complete at the clear give no image.  A
     complete image after the clear works.
   - Refused: a file with a missing message, or a complete file that
     is not a PNG file, gives no image.  A complete image after it
     works.
   - Replace: a second complete image with the ID of the first one
     replaces it.  The check of the Makefile with sanitizers finds the
     leak if the store does not free the first one.

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

#include "images.h"
#include "rdpng.h"
#include "rdproto.h"

enum { TRIALS = 100 };

/* The largest side of a random image, in pixels.  */
enum { MAX_SIDE = 48 };

/* The largest number of images of one trial.  */
enum { MAX_IMAGES = 8 };

/* The PNG file of an image, and its ID.  */
struct png
{
  uint32_t id;
  unsigned char *bytes;
  size_t size;
};

static const char *run_trial (uint64_t);
static bool join_law (uint64_t *);
static bool forget_law (uint64_t *);
static bool clear_law (uint64_t *);
static bool refused_law (uint64_t *);
static bool replace_law (uint64_t *);
static bool add_png (struct rc_images *, struct png, uint64_t *);
static bool add_png_part (struct rc_images *, struct png, uint64_t *,
			  size_t, size_t);
static void add_part (struct rc_images *, struct png, size_t, size_t);
static size_t random_split (uint64_t *, size_t);
static bool add_random_images (struct rc_images *, uint64_t *,
			       uint32_t *, int);
static bool has_png_p (struct rc_images *, struct png, cairo_surface_t *);
static struct png random_png (uint64_t *, uint32_t, cairo_surface_t **);
static cairo_surface_t *random_surface (uint64_t *);
static uint32_t random_premultiplied_pixel (uint64_t *);
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
    { "join", join_law },
    { "forget", forget_law },
    { "clear", clear_law },
    { "refused", refused_law },
    { "replace", replace_law },
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
		   "test-images: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-images: %d trials passed\n", TRIALS);
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
join_law (uint64_t *state)
{
  struct rc_images *images = rc_images_new ();
  cairo_surface_t *surface;
  struct png png = random_png (state, random_next (state), &surface);
  bool passed = (add_png_part (images, png, state, 0, png.size - 1)
		 && rc_images_find (images, png.id) == NULL);
  add_part (images, png, png.size - 1, 1);
  passed = passed && has_png_p (images, png, surface);
  cairo_surface_destroy (surface);
  free (png.bytes);
  rc_images_free (images);
  return passed;
}

static bool
forget_law (uint64_t *state)
{
  struct rc_images *images = rc_images_new ();
  uint32_t ids[MAX_IMAGES];
  int count = random_below (state, MAX_IMAGES) + 1;
  bool passed = add_random_images (images, state, ids, count);
  uint32_t forgotten = ids[random_below (state, count)];
  rc_images_forget (images, forgotten);
  rc_images_forget (images, UINT32_MAX);
  for (int i = 0; passed && i < count; i++)
    passed = ((rc_images_find (images, ids[i]) == NULL)
	      == (ids[i] == forgotten));
  rc_images_free (images);
  return passed;
}

static bool
clear_law (uint64_t *state)
{
  struct rc_images *images = rc_images_new ();
  uint32_t ids[MAX_IMAGES];
  int count = random_below (state, MAX_IMAGES) + 1;
  bool passed = add_random_images (images, state, ids, count);
  cairo_surface_t *surface;
  struct png late = random_png (state, UINT32_MAX, &surface);
  size_t split = random_below (state, late.size - 1) + 1;
  add_png_part (images, late, state, 0, split);
  rc_images_clear (images);
  add_png_part (images, late, state, split, late.size);
  for (int i = 0; passed && i < count; i++)
    passed = rc_images_find (images, ids[i]) == NULL;
  passed = passed && rc_images_find (images, late.id) == NULL;
  passed = passed && add_png (images, late, state);
  passed = passed && has_png_p (images, late, surface);
  cairo_surface_destroy (surface);
  free (late.bytes);
  rc_images_free (images);
  return passed;
}

static bool
refused_law (uint64_t *state)
{
  struct rc_images *images = rc_images_new ();
  cairo_surface_t *surface;
  struct png png = random_png (state, 1, &surface);
  size_t gap = random_below (state, png.size - 1) + 1;
  add_png_part (images, png, state, 0, gap);
  add_png_part (images, png, state, gap + 1, png.size);
  bool passed = rc_images_find (images, png.id) == NULL;
  unsigned char noise[64];
  for (size_t i = 0; i < sizeof noise; i++)
    noise[i] = random_next (state);
  struct png not_png = { 2, noise, random_below (state, sizeof noise) + 1 };
  passed = passed && add_png (images, not_png, state);
  passed = passed && rc_images_find (images, not_png.id) == NULL;
  png.id = 3;
  passed = passed && add_png (images, png, state);
  passed = passed && has_png_p (images, png, surface);
  cairo_surface_destroy (surface);
  free (png.bytes);
  rc_images_free (images);
  return passed;
}

static bool
replace_law (uint64_t *state)
{
  struct rc_images *images = rc_images_new ();
  uint32_t id = random_next (state);
  cairo_surface_t *first, *second;
  struct png a = random_png (state, id, &first);
  struct png b = random_png (state, id, &second);
  bool passed = (add_png (images, a, state)
		 && has_png_p (images, a, first)
		 && add_png (images, b, state)
		 && has_png_p (images, b, second));
  cairo_surface_destroy (first);
  cairo_surface_destroy (second);
  free (a.bytes);
  free (b.bytes);
  rc_images_free (images);
  return passed;
}

/* Add all bytes of PNG to IMAGES in parts of random sizes.  Return
   false if PNG has no bytes.  */
static bool
add_png (struct rc_images *images, struct png png, uint64_t *state)
{
  return png.size > 0 && add_png_part (images, png, state, 0, png.size);
}

/* Add the bytes of PNG from START to END to IMAGES, in parts of random
   sizes.  Return true.  */
static bool
add_png_part (struct rc_images *images, struct png png, uint64_t *state,
	      size_t start, size_t end)
{
  for (size_t offset = start; offset < end;)
    {
      size_t nbytes = random_split (state, end - offset);
      add_part (images, png, offset, nbytes);
      offset += nbytes;
    }
  return true;
}

/* Add one image-data message with the NBYTES bytes of PNG at OFFSET
   to IMAGES.  NBYTES is at most RD_MAX_IMAGE_CHUNK.  */
static void
add_part (struct rc_images *images, struct png png, size_t offset,
	  size_t nbytes)
{
  struct rd_message message = { .type = RD_IMAGE_DATA };
  struct rd_image_data *data = &message.u.image_data;
  data->image = png.id;
  data->offset = offset;
  data->total = png.size;
  data->nbytes = nbytes;
  memcpy (data->bytes, png.bytes + offset, nbytes);
  rc_images_add (images, &message);
}

/* Return the size of a random part of a file with LEFT more bytes.
   Some parts have the maximum size.  */
static size_t
random_split (uint64_t *state, size_t left)
{
  size_t most = left < RD_MAX_IMAGE_CHUNK ? left : RD_MAX_IMAGE_CHUNK;
  if (random_below (state, 4) == 0)
    return most;
  return random_below (state, most) + 1;
}

/* Add COUNT random images with different IDs to IMAGES, and set IDS to
   their IDs.  Return true if each image is in IMAGES after its
   messages.  */
static bool
add_random_images (struct rc_images *images, uint64_t *state,
		   uint32_t *ids, int count)
{
  bool passed = true;
  for (int i = 0; passed && i < count; i++)
    {
      cairo_surface_t *surface;
      ids[i] = (uint32_t) random_below (state, 1u << 16) * MAX_IMAGES + i;
      struct png png = random_png (state, ids[i], &surface);
      passed = (add_png (images, png, state)
		&& has_png_p (images, png, surface));
      cairo_surface_destroy (surface);
      free (png.bytes);
    }
  return passed;
}

/* Return true if IMAGES has an image for the ID of PNG with the size
   and the pixels of SURFACE.  */
static bool
has_png_p (struct rc_images *images, struct png png,
	   cairo_surface_t *surface)
{
  cairo_surface_t *image = rc_images_find (images, png.id);
  return image && same_pixels_p (surface, image);
}

/* Return the PNG file of a random surface with the ID ID, and set
   *SURFACE to the surface.  */
static struct png
random_png (uint64_t *state, uint32_t id, cairo_surface_t **surface)
{
  struct png png = { id, NULL, 0 };
  *surface = random_surface (state);
  png.bytes = rd_png_encode (*surface, &png.size);
  if (!png.bytes)
    abort ();
  return png;
}

/* Return a surface with a random format, a random size and random
   pixels.  An ARGB32 surface has valid premultiplied pixels.  */
static cairo_surface_t *
random_surface (uint64_t *state)
{
  cairo_format_t format = (random_below (state, 2) ? CAIRO_FORMAT_ARGB32
			   : CAIRO_FORMAT_RGB24);
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
