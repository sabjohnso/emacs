/* The cache of font file hashes of the remote display client.

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

/* A hash cache keeps the hash of each file with the size and the
   modification time of the file.  An entry is valid only while the
   file has that size and that time.  A cache can have a file, which
   keeps the entries for the next process.  */

#ifndef REMOTE_CLIENT_HASHCACHE_H
#define REMOTE_CLIENT_HASHCACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

#include "rdproto.h"

struct rc_hash_cache;

/* Return a new cache with the entries of the cache file NAME.  If NAME
   is NULL, the cache has no file.  A file that does not exist, and a
   line of the file with another form, give no entries.  */
extern struct rc_hash_cache *rc_hash_cache_load (const char *name);
extern void rc_hash_cache_free (struct rc_hash_cache *cache);

/* Set HASH to the hash of the file NAME with the status ST, and return
   true.  Return false if CACHE has no valid entry for the file.  */
extern bool rc_hash_cache_get (const struct rc_hash_cache *cache,
			       const char *name, const struct stat *st,
			       uint8_t hash[RD_HASH_SIZE]);

/* Record HASH for the file NAME with the status ST in CACHE.  */
extern void rc_hash_cache_put (struct rc_hash_cache *cache,
			       const char *name, const struct stat *st,
			       const uint8_t hash[RD_HASH_SIZE]);

/* Write CACHE to its file, if it has a file and changed since the last
   load or the last save that succeeded.  Make the directory of the
   file if it does not exist.  */
extern void rc_hash_cache_save (struct rc_hash_cache *cache);

/* Set HEX to the 2 * RD_HASH_SIZE hexadecimal digits of HASH, and a
   null character.  */
extern void rc_hash_to_hex (const uint8_t hash[RD_HASH_SIZE],
			    char hex[2 * RD_HASH_SIZE + 1]);

#endif /* REMOTE_CLIENT_HASHCACHE_H */
