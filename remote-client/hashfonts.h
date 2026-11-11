/* The font files by hash of the remote display client.

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

/* A hash source finds a font file by its SHA-256 hash, the identity
   of a font in a define-font message.  The hash of all font files of
   a system can take seconds, so the source hashes a file only when a
   request needs it.  A cache file keeps the hash of each file for the
   next source.  */

#ifndef REMOTE_CLIENT_HASHFONTS_H
#define REMOTE_CLIENT_HASHFONTS_H

#include <stdbool.h>
#include <stdint.h>

#include "rdproto.h"
#include "render.h"

/* A function that sets HASH to the SHA-256 hash of the file NAME.  It
   returns false if it cannot read the file.  DATA is the value that
   the maker of the source gave with the function.  */
typedef bool rc_hash_function (void *data, const char *name,
			       uint8_t hash[RD_HASH_SIZE]);

/* The hash function of the client.  It ignores DATA.  */
extern rc_hash_function rc_sha256_file;

/* Return a new font source that finds a font file by its hash among
   the NFILES file names FILES.  The source hashes the files with HASH
   and DATA, in the order of FILES.  CACHE is the name of the cache
   file, or NULL for no cache file.  The source writes the cache file
   after each request that hashed a file.  The source copies FILES and
   CACHE.  */
extern struct rc_font_source rc_hash_font_source_new
  (const char *const *files, int nfiles, const char *cache,
   rc_hash_function *hash, void *data);

/* Return the names of the font files of fontconfig, each name one
   time, in a new array that ends with NULL.  Free it with
   g_strfreev.  */
extern char **rc_font_file_names (void);

#endif /* REMOTE_CLIENT_HASHFONTS_H */
