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

/* The client cannot continue with no memory, so a failed allocation
   stops the program, as in GLib.  */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "fonts.h"
#include "fontstore.h"
#include "hashcache.h"

/* A request for the file with the hash HASH and SIZE bytes.  RECEIVED
   bytes came, and the store wrote them to the temporary file PARTIAL
   through FD, and gave them to CHECKSUM.  Before the first bytes,
   PARTIAL is NULL and FD is -1.  After a refusal, FAILED is true, and
   the request has no temporary file.  */
struct request
{
  uint8_t hash[RD_HASH_SIZE];
  uint32_t size;
  uint32_t received;
  bool failed;
  char *partial;
  int fd;
  GChecksum *checksum;
};

struct rc_font_store
{
  char *directory;
  /* The requests, each a struct request.  */
  GPtrArray *requests;
};

static cairo_font_face_t *find_in_store (void *,
					 const struct rc_font_key *);
static struct request *find_request (struct rc_font_store *,
				     const uint8_t *);
static bool start_file (struct rc_font_store *, struct request *);
static bool write_bytes (struct request *, const uint8_t *, uint32_t);
static enum rc_font_result finish_file (struct rc_font_store *,
					struct request *);
static enum rc_font_result refuse (struct request *);
static void drop_partial (struct request *);
static void free_request (void *);
static char *stored_name (const struct rc_font_store *, const uint8_t *);

struct rc_font_store *
rc_font_store_new (const char *directory)
{
  g_mkdir_with_parents (directory, 0700);
  struct rc_font_store *store = g_new (struct rc_font_store, 1);
  store->directory = g_strdup (directory);
  store->requests = g_ptr_array_new_with_free_func (free_request);
  return store;
}

void
rc_font_store_free (struct rc_font_store *store)
{
  if (!store)
    return;
  g_ptr_array_free (store->requests, TRUE);
  g_free (store->directory);
  g_free (store);
}

struct rc_font_source
rc_font_store_source (struct rc_font_store *store)
{
  return (struct rc_font_source) { find_in_store, NULL, store };
}

bool
rc_font_store_want (struct rc_font_store *store,
		    const struct rc_font_key *key)
{
  if (key->nhash != RD_HASH_SIZE || key->file_size == 0
      || key->file_size > RC_FONT_STORE_LIMIT
      || find_request (store, key->hash))
    return false;
  char *name = stored_name (store, key->hash);
  bool stored = g_file_test (name, G_FILE_TEST_IS_REGULAR);
  g_free (name);
  if (stored)
    return false;
  struct request *request = g_new0 (struct request, 1);
  memcpy (request->hash, key->hash, RD_HASH_SIZE);
  request->size = key->file_size;
  request->fd = -1;
  g_ptr_array_add (store->requests, request);
  return true;
}

enum rc_font_result
rc_font_store_receive (struct rc_font_store *store,
		       const struct rd_font_data *data)
{
  struct request *request = (data->nhash == RD_HASH_SIZE
			     ? find_request (store, data->hash) : NULL);
  if (!request || request->failed)
    return RC_FONT_REFUSED;
  if (data->offset != request->received || data->total != request->size)
    return refuse (request);
  if ((!request->partial && !start_file (store, request))
      || !write_bytes (request, data->bytes, data->nbytes))
    return refuse (request);
  /* Bytes after the end make the hash wrong, so the file is refused.  */
  if (request->received < request->size)
    return RC_FONT_PARTIAL;
  return finish_file (store, request);
}

void
rc_font_store_reset (struct rc_font_store *store)
{
  g_ptr_array_set_size (store->requests, 0);
}

/* The FIND function of the source of a store.  DATA is the store.  */
static cairo_font_face_t *
find_in_store (void *data, const struct rc_font_key *key)
{
  if (key->nhash != RD_HASH_SIZE)
    return NULL;
  char *name = stored_name (data, key->hash);
  cairo_font_face_t *face = (g_file_test (name, G_FILE_TEST_IS_REGULAR)
			     ? rc_file_face (name, key->index) : NULL);
  g_free (name);
  return face;
}

/* Return the request of STORE for the file with the hash HASH, or
   NULL.  */
static struct request *
find_request (struct rc_font_store *store, const uint8_t *hash)
{
  for (guint i = 0; i < store->requests->len; i++)
    {
      struct request *request = g_ptr_array_index (store->requests, i);
      if (!memcmp (request->hash, hash, RD_HASH_SIZE))
	return request;
    }
  return NULL;
}

/* Make the temporary file of REQUEST in the directory of STORE.
   Return false on an error.  */
static bool
start_file (struct rc_font_store *store, struct request *request)
{
  request->partial = g_build_filename (store->directory, ".partial-XXXXXX",
				       NULL);
  request->fd = g_mkstemp_full (request->partial, O_WRONLY | O_CLOEXEC,
				0644);
  if (request->fd < 0)
    {
      g_free (request->partial);
      request->partial = NULL;
      return false;
    }
  request->checksum = g_checksum_new (G_CHECKSUM_SHA256);
  return true;
}

/* Write the SIZE bytes BYTES to the temporary file of REQUEST, and give
   them to its checksum.  Return false on an error.  */
static bool
write_bytes (struct request *request, const uint8_t *bytes, uint32_t size)
{
  for (uint32_t done = 0; done < size; )
    {
      ssize_t n = write (request->fd, bytes + done, size - done);
      if (n < 0 && errno != EINTR)
	return false;
      done += n < 0 ? 0 : n;
    }
  g_checksum_update (request->checksum, bytes, size);
  request->received += size;
  return true;
}

/* Check the hash of the complete file of REQUEST.  If it is correct,
   give the file its name in STORE, and remove REQUEST.  */
static enum rc_font_result
finish_file (struct rc_font_store *store, struct request *request)
{
  uint8_t hash[RD_HASH_SIZE];
  gsize length = RD_HASH_SIZE;
  g_checksum_get_digest (request->checksum, hash, &length);
  bool closed = close (request->fd) == 0;
  request->fd = -1;
  if (!closed || memcmp (hash, request->hash, RD_HASH_SIZE))
    return refuse (request);
  char *name = stored_name (store, request->hash);
  bool renamed = g_rename (request->partial, name) == 0;
  g_free (name);
  if (!renamed)
    return refuse (request);
  g_free (request->partial);
  request->partial = NULL;
  g_ptr_array_remove_fast (store->requests, request);
  return RC_FONT_DONE;
}

/* Mark REQUEST as failed, delete its temporary file, and return
   RC_FONT_REFUSED.  */
static enum rc_font_result
refuse (struct request *request)
{
  drop_partial (request);
  request->failed = true;
  return RC_FONT_REFUSED;
}

/* Close and delete the temporary file of REQUEST, if it has one.  */
static void
drop_partial (struct request *request)
{
  if (request->fd >= 0)
    close (request->fd);
  request->fd = -1;
  if (request->partial)
    g_unlink (request->partial);
  g_free (request->partial);
  request->partial = NULL;
  if (request->checksum)
    g_checksum_free (request->checksum);
  request->checksum = NULL;
}

/* Free REQUEST, a struct request, and delete its temporary file.  */
static void
free_request (void *data)
{
  drop_partial (data);
  g_free (data);
}

/* Return the name of the file of STORE with the hash HASH.  */
static char *
stored_name (const struct rc_font_store *store, const uint8_t *hash)
{
  char hex[2 * RD_HASH_SIZE + 1];
  rc_hash_to_hex (hash, hex);
  return g_build_filename (store->directory, hex, NULL);
}
