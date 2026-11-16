/* The image store of the remote display client.

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

/* The store joins the messages of a file with rdchunk.c, decodes the
   file with rdpng.c, and keeps the surface of each ID in a table.
   The client cannot continue with no memory, so a failed allocation
   stops the program.  */

#include <config.h>

#include <stdlib.h>

#include "images.h"
#include "rdchunk.h"
#include "rdpng.h"
#include "table.h"

struct rc_images
{
  /* The surface of each image ID.  */
  struct rc_table surfaces;
  /* The file that comes now.  */
  struct rd_chunks file;
};

static void keep_file (struct rc_images *);
static void keep (struct rc_images *, uint32_t, cairo_surface_t *);
static void destroy_surface (void *);

struct rc_images *
rc_images_new (void)
{
  struct rc_images *store = calloc (1, sizeof *store);
  if (!store)
    abort ();
  return store;
}

void
rc_images_free (struct rc_images *store)
{
  rc_images_clear (store);
  free (store);
}

void
rc_images_add (struct rc_images *store, const struct rd_message *message)
{
  if (message->type != RD_IMAGE_DATA)
    return;
  switch (rd_chunks_add_message (&store->file, message, RD_MAX_IMAGE_SIZE))
    {
    case RD_CHUNK_PARTIAL:
      break;
    case RD_CHUNK_DONE:
      keep_file (store);
      break;
    case RD_CHUNK_REFUSED:
      rd_chunks_free (&store->file);
      break;
    }
}

cairo_surface_t *
rc_images_find (const struct rc_images *store, uint32_t id)
{
  return rc_table_get (&store->surfaces, id);
}

void
rc_images_forget (struct rc_images *store, uint32_t id)
{
  destroy_surface (rc_table_remove (&store->surfaces, id));
}

void
rc_images_clear (struct rc_images *store)
{
  rc_table_free (&store->surfaces, destroy_surface);
  rd_chunks_free (&store->file);
}

/* Decode the complete file of STORE, and give its ID the pixels.  Free
   the bytes of the file.  A file that is not a PNG file gives no
   image.  */
static void
keep_file (struct rc_images *store)
{
  cairo_surface_t *surface = rd_png_decode (store->file.bytes,
					    store->file.total);
  uint32_t id = store->file.serial;
  rd_chunks_free (&store->file);
  if (surface)
    keep (store, id, surface);
}

/* Give the image ID of STORE the pixels SURFACE, and free the pixels
   that ID had before.  */
static void
keep (struct rc_images *store, uint32_t id, cairo_surface_t *surface)
{
  rc_images_forget (store, id);
  rc_table_set (&store->surfaces, id, surface);
}

/* Free the surface SURFACE, which can be NULL.  */
static void
destroy_surface (void *surface)
{
  cairo_surface_destroy (surface);
}
