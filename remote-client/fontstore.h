/* The store of the font files that the server sends to remote-client.

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

/* When the client has no font file for a define-font message, it asks
   the server for the file with a want-font message.  The server sends
   the file in font-data messages, in order.  The store writes the bytes
   to a temporary file in its directory.  After the last byte, it
   checks the SHA-256 hash, and renames the file to the hash in
   hexadecimal.  Thus a file in the directory always has the bytes of
   its name.  The source of the store finds a font by its hash in the
   directory, with no hash of a file.

   The store accepts only the bytes of a file that it asked for, in
   order, with the file size of the define-font message.  */

#ifndef REMOTE_CLIENT_FONTSTORE_H
#define REMOTE_CLIENT_FONTSTORE_H

#include <stdbool.h>
#include <stdint.h>

#include "rdproto.h"
#include "render.h"

/* The largest font file that the store asks for, in bytes.  */
enum { RC_FONT_STORE_LIMIT = 64 * 1024 * 1024 };

struct rc_font_store;

/* Return a new store in the directory DIRECTORY.  Make the directory
   if it does not exist.  The store copies DIRECTORY.  */
extern struct rc_font_store *rc_font_store_new (const char *directory);

/* Free STORE, and delete its temporary files.  STORE can be NULL.  */
extern void rc_font_store_free (struct rc_font_store *store);

/* Return a new font source that finds the font KEY in the directory of
   STORE.  The source does not own STORE, so STORE must stay valid
   until the source is freed.  */
extern struct rc_font_source rc_font_store_source
  (struct rc_font_store *store);

/* Record that the client needs the file of KEY.  Return true if the
   client must send a want-font message for it: KEY has a full hash, a
   file size from 1 to RC_FONT_STORE_LIMIT, and the store has no file
   and no earlier request for it.  */
extern bool rc_font_store_want (struct rc_font_store *store,
				const struct rc_font_key *key);

/* The results of rc_font_store_receive.  */
enum rc_font_result
  {
    /* The bytes are correct, and more bytes must come.  */
    RC_FONT_PARTIAL,
    /* The file is complete, and has the hash of its request.  The
       source of the store finds it now.  */
    RC_FONT_DONE,
    /* The store did not ask for these bytes, or they are not correct.
       The store deletes the temporary file of the request, and does
       not ask for the file again until rc_font_store_reset.  */
    RC_FONT_REFUSED,
  };

/* Add the bytes of the font-data message DATA to STORE.  */
extern enum rc_font_result rc_font_store_receive
  (struct rc_font_store *store, const struct rd_font_data *data);

/* Forget each request of STORE, and delete its temporary files.  The
   client calls this when it connects again, because the server sends
   the file of a request only on the connection of the request.  */
extern void rc_font_store_reset (struct rc_font_store *store);

#endif /* REMOTE_CLIENT_FONTSTORE_H */
