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

/* A store keeps the pixels of each image ID that the server sent.
   The server sends the PNG file of an image in image-data messages
   (rdproto.h).  When the last message of a file comes, the store
   decodes the file, and keeps the pixels for the ID.  The module does
   not use GTK, so a test can use it with no display.  */

#ifndef REMOTE_CLIENT_IMAGES_H
#define REMOTE_CLIENT_IMAGES_H

#include <stdint.h>

#include <cairo.h>

#include "rdproto.h"

struct rc_images;

/* Return a new store with no images.  */
extern struct rc_images *rc_images_new (void);

/* Free STORE and its images.  */
extern void rc_images_free (struct rc_images *store);

/* Add the image-data message MESSAGE to STORE.  After the last message
   of a PNG file, the ID of the file has the pixels of the file, and an
   image that the ID had before is freed.  A file that STORE cannot
   join or decode gives no image.  STORE ignores a message of another
   type.  */
extern void rc_images_add (struct rc_images *store,
			   const struct rd_message *message);

/* Return the pixels of the image ID in STORE, or NULL if STORE has no
   image ID.  The surface stays valid until the next change of
   STORE.  */
extern cairo_surface_t *rc_images_find (const struct rc_images *store,
					uint32_t id);

/* Free the image ID of STORE, if STORE has it.  */
extern void rc_images_forget (struct rc_images *store, uint32_t id);

/* Free all images of STORE, and drop a file that is not complete.  */
extern void rc_images_clear (struct rc_images *store);

#endif /* REMOTE_CLIENT_IMAGES_H */
