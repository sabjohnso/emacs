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

/* The client cannot continue with no memory, so a failed allocation
   stops the program, as in GLib.  */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <glib.h>

#include "hashcache.h"
#include "hashfonts.h"

/* A file of the list of a source.  READABLE is true if HASH has its
   hash.  */
struct listed_file
{
  char *name;
  bool readable;
  uint8_t hash[RD_HASH_SIZE];
};

/* The data of a hash source.  The source has tried to hash each file
   of FILES before NEXT, and no file after it.  The cache has each file
   that the source hashed, so a name that comes again in the list needs
   no new hash.  */
struct hash_source
{
  struct listed_file *files;
  int nfiles;
  int next;
  rc_hash_function *hash;
  void *data;
  struct rc_hash_cache *cache;
};

static cairo_font_face_t *find_by_hash (void *, const uint8_t *, int,
					uint32_t);
static void destroy_hash_source (void *);
static void hash_listed_file (struct hash_source *, struct listed_file *);
static cairo_font_face_t *file_face (const char *, uint32_t);

struct rc_font_source
rc_hash_font_source_new (const char *const *files, int nfiles,
			 const char *cache, rc_hash_function *hash,
			 void *data)
{
  struct hash_source *source = g_new0 (struct hash_source, 1);
  source->files = g_new0 (struct listed_file, nfiles);
  for (int i = 0; i < nfiles; i++)
    source->files[i].name = g_strdup (files[i]);
  source->nfiles = nfiles;
  source->hash = hash;
  source->data = data;
  source->cache = rc_hash_cache_load (cache);
  return (struct rc_font_source) { find_by_hash, destroy_hash_source,
				   source };
}

bool
rc_sha256_file (void *data, const char *name, uint8_t hash[RD_HASH_SIZE])
{
  (void) data;
  FILE *file = fopen (name, "rb");
  if (!file)
    return false;
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA256);
  unsigned char buffer[65536];
  size_t n;
  while ((n = fread (buffer, 1, sizeof buffer, file)) > 0)
    g_checksum_update (checksum, buffer, n);
  bool read = !ferror (file);
  fclose (file);
  gsize length = RD_HASH_SIZE;
  g_checksum_get_digest (checksum, hash, &length);
  g_checksum_free (checksum);
  return read;
}

/* FcFontList gives each pattern one time, and the patterns have only
   the file name, so each file name comes one time.  */
char **
rc_font_file_names (void)
{
  FcPattern *all = FcPatternCreate ();
  FcObjectSet *objects = FcObjectSetBuild (FC_FILE, NULL);
  FcFontSet *set = FcFontList (NULL, all, objects);
  FcObjectSetDestroy (objects);
  FcPatternDestroy (all);
  GPtrArray *names = g_ptr_array_new ();
  for (int i = 0; set && i < set->nfont; i++)
    {
      FcChar8 *name;
      if (FcPatternGetString (set->fonts[i], FC_FILE, 0, &name)
	  == FcResultMatch)
	g_ptr_array_add (names, g_strdup ((const char *) name));
    }
  g_ptr_array_add (names, NULL);
  if (set)
    FcFontSetDestroy (set);
  return (char **) g_ptr_array_free (names, FALSE);
}

/* The FIND function of a hash source.  DATA is the struct
   hash_source.  Look at the files in order, and hash a file when the
   search gets to it for the first time.  Then write the cache, if a
   file got a hash.  */
static cairo_font_face_t *
find_by_hash (void *data, const uint8_t *hash, int nhash, uint32_t index)
{
  struct hash_source *source = data;
  if (nhash != RD_HASH_SIZE)
    return NULL;
  cairo_font_face_t *face = NULL;
  for (int i = 0; !face && i < source->nfiles; i++)
    {
      struct listed_file *file = &source->files[i];
      if (i == source->next)
	{
	  hash_listed_file (source, file);
	  source->next++;
	}
      if (file->readable && !memcmp (file->hash, hash, RD_HASH_SIZE))
	face = file_face (file->name, index);
    }
  rc_hash_cache_save (source->cache);
  return face;
}

/* The DESTROY function of a hash source.  DATA is the struct
   hash_source.  */
static void
destroy_hash_source (void *data)
{
  struct hash_source *source = data;
  for (int i = 0; i < source->nfiles; i++)
    g_free (source->files[i].name);
  g_free (source->files);
  rc_hash_cache_free (source->cache);
  g_free (source);
}

/* Set the hash of FILE from the cache of SOURCE.  If the cache has no
   valid hash, hash FILE, and put the hash in the cache.  A file that
   does not exist has no hash.  */
static void
hash_listed_file (struct hash_source *source, struct listed_file *file)
{
  struct stat st;
  if (stat (file->name, &st) != 0)
    return;
  if (rc_hash_cache_get (source->cache, file->name, &st, file->hash))
    file->readable = true;
  else if (source->hash (source->data, file->name, file->hash))
    {
      file->readable = true;
      rc_hash_cache_put (source->cache, file->name, &st, file->hash);
    }
}

/* Return a new reference to the face INDEX of the font file NAME.  */
static cairo_font_face_t *
file_face (const char *name, uint32_t index)
{
  FcPattern *pattern = FcPatternBuild (NULL, FC_FILE, FcTypeString, name,
				       FC_INDEX, FcTypeInteger, (int) index,
				       NULL);
  FcDefaultSubstitute (pattern);
  cairo_font_face_t *face = cairo_ft_font_face_create_for_pattern (pattern);
  FcPatternDestroy (pattern);
  return face;
}
