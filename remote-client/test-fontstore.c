/* Test the store of the font files of remote-client.

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

/* The laws of a font store.  A file of a trial has random bytes and a
   random size.  The server of a trial sends it in chunks of random
   sizes, in order.

   - Delivery: after a want, the chunks of the file give
     RC_FONT_PARTIAL, and the last chunk gives RC_FONT_DONE.  Then the
     directory has one file, its name is the hash in hexadecimal, and
     it has the bytes of the file.  Two files whose chunks come in
     turns both arrive.
   - Damage: if one byte of one chunk is not correct, the last chunk
     gives RC_FONT_REFUSED.  If a chunk has an offset that is not the
     next offset, a total that is not the file size of the want, or
     bytes after the end of the file, that chunk gives
     RC_FONT_REFUSED.  After a refusal, the directory has no file, a
     correct send of the whole file again gives RC_FONT_REFUSED, and a
     want for the same file returns false.
   - Unasked: a chunk of a file with no want gives RC_FONT_REFUSED, and
     the directory has no file.
   - Want: a want returns true for a key with a full hash and a file
     size from 1 to RC_FONT_STORE_LIMIT, the first time.  It returns
     false a second time, for a file that the store has, and for a
     hash with fewer bytes or a file size out of the range.  After a
     reset, the directory has no partial file, and a want for the file
     returns true again.
   - Keep: a new store in the same directory has the files of the old
     store: its source finds them, and a want for them returns false.
     The source finds no face for a key with a short hash.

   The examples:

   - Face: after the delivery of the monospace font file of
     fontconfig, the source gives a face of that file, with the face
     index of the key.
   - Unwritable: when the store cannot make a file in its directory,
     the first chunk gives RC_FONT_REFUSED, and the directory has no
     file.

   With no argument, the program runs the examples and TRIALS trials
   from a seed that it gets from the clock and the process ID.  With
   the argument SEED, it runs only the trial of that seed.  */

#include <config.h>

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "fontstore.h"
#include "hashcache.h"

enum { TRIALS = 200 };

/* The largest file of a trial, in bytes.  */
enum { MAX_TRIAL_FILE = 40000 };

/* A file of a trial: its bytes, its size, and its hash.  */
struct test_file
{
  uint8_t *bytes;
  uint32_t size;
  uint8_t hash[RD_HASH_SIZE];
};

/* A law and its name.  */
struct law
{
  const char *name;
  bool (*check) (const char *, uint64_t *);
};

static const char *run_trial (uint64_t);
static bool delivery_law (const char *, uint64_t *);
static bool turns_law (const char *, uint64_t *);
static bool damage_law (const char *, uint64_t *);
static bool unasked_law (const char *, uint64_t *);
static bool want_law (const char *, uint64_t *);
static bool keep_law (const char *, uint64_t *);
static bool face_example (void);
static bool unwritable_example (void);
static struct test_file random_file (uint64_t *);
static void free_file (struct test_file *);
static struct rc_font_key key_of (const struct test_file *);
static bool want (struct rc_font_store *, const struct test_file *);
static enum rc_font_result send_chunk (struct rc_font_store *,
				       const struct test_file *, uint32_t,
				       uint32_t);
static enum rc_font_result send_file (struct rc_font_store *,
				      const struct test_file *, uint64_t *);
static uint32_t chunk_size (const struct test_file *, uint32_t,
			    uint64_t *);
static bool has_only (const char *, const struct test_file *);
static int count_files (const char *);
static char *new_directory (void);
static void remove_directory (char *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

static const struct law laws[] =
  {
    { "delivery", delivery_law },
    { "turns", turns_law },
    { "damage", damage_law },
    { "unasked", unasked_law },
    { "want", want_law },
    { "keep", keep_law },
  };

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 1 : 0;
  if (!face_example () || !unwritable_example ())
    return 1;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    {
      const char *failed = run_trial (base + trial);
      if (failed)
	{
	  fprintf (stderr,
		   "test-fontstore: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-fontstore: the examples and %d trials passed\n", TRIALS);
  return 0;
}

/* Run each law with SEED in a new directory.  Return the name of the
   first law that fails, or NULL.  */
static const char *
run_trial (uint64_t seed)
{
  for (size_t i = 0; i < sizeof laws / sizeof *laws; i++)
    {
      uint64_t state = seed;
      char *directory = new_directory ();
      char *fonts = g_build_filename (directory, "fonts", NULL);
      bool passed = laws[i].check (fonts, &state);
      g_free (fonts);
      remove_directory (directory);
      if (!passed)
	return laws[i].name;
    }
  return NULL;
}

/* The laws.  Each one gets the name of a directory that does not
   exist.  */

static bool
delivery_law (const char *directory, uint64_t *state)
{
  struct rc_font_store *store = rc_font_store_new (directory);
  struct test_file file = random_file (state);
  bool passed = (want (store, &file)
		 && send_file (store, &file, state) == RC_FONT_DONE
		 && has_only (directory, &file));
  free_file (&file);
  rc_font_store_free (store);
  return passed;
}

static bool
turns_law (const char *directory, uint64_t *state)
{
  struct rc_font_store *store = rc_font_store_new (directory);
  struct test_file files[2] = { random_file (state), random_file (state) };
  uint32_t offsets[2] = { 0, 0 };
  bool passed = want (store, &files[0]) && want (store, &files[1]);
  int done = 0;
  while (passed && done < 2)
    {
      int which = (offsets[0] == files[0].size ? 1
		   : offsets[1] == files[1].size ? 0
		   : (int) random_below (state, 2));
      struct test_file *file = &files[which];
      uint32_t size = chunk_size (file, offsets[which], state);
      enum rc_font_result result = send_chunk (store, file,
					       offsets[which], size);
      offsets[which] += size;
      bool last = offsets[which] == file->size;
      passed = result == (last ? RC_FONT_DONE : RC_FONT_PARTIAL);
      done += last;
    }
  passed = passed && count_files (directory) == 2;
  free_file (&files[0]);
  free_file (&files[1]);
  rc_font_store_free (store);
  return passed;
}

/* The kinds of damage of the damage law.  */
enum damage { BAD_BYTE, BAD_OFFSET, BAD_TOTAL, PAST_END, DAMAGE_KINDS };

static bool
damage_law (const char *directory, uint64_t *state)
{
  struct rc_font_store *store = rc_font_store_new (directory);
  struct test_file file = random_file (state);
  enum damage damage = random_below (state, DAMAGE_KINDS);
  /* The chunk with the offset AT has the damage.  For PAST_END, the
     last chunk has it.  */
  uint32_t at = random_below (state, file.size);
  bool passed = want (store, &file);
  uint32_t offset = 0;
  enum rc_font_result result = RC_FONT_PARTIAL;
  while (passed && result == RC_FONT_PARTIAL && offset < file.size)
    {
      uint32_t size = chunk_size (&file, offset, state);
      /* A chunk past the end must fit in a message.  */
      if (damage == PAST_END && size == RD_MAX_FONT_CHUNK)
	size--;
      bool damaged = (damage == PAST_END ? offset + size == file.size
		      : offset <= at && at < offset + size);
      struct rd_message message = { .type = RD_FONT_DATA };
      struct rd_font_data *data = &message.u.font_data;
      data->nhash = RD_HASH_SIZE;
      memcpy (data->hash, file.hash, RD_HASH_SIZE);
      data->offset = offset;
      data->total = file.size;
      data->nbytes = size;
      memcpy (data->bytes, file.bytes + offset, size);
      if (damaged && damage == BAD_BYTE)
	data->bytes[at - offset] ^= 1 << random_below (state, 8);
      else if (damaged && damage == BAD_OFFSET)
	data->offset += 1 + random_below (state, 3);
      else if (damaged && damage == BAD_TOTAL)
	data->total = file.size + 1 + random_below (state, 3);
      else if (damaged && damage == PAST_END)
	/* The last chunk has one byte after the end of the file.  */
	data->nbytes = size + 1;
      result = rc_font_store_receive (store, data);
      offset += size;
      bool refused_now = (damaged && damage != BAD_BYTE)
			 || (damage == BAD_BYTE && offset == file.size);
      if (refused_now)
	passed = result == RC_FONT_REFUSED;
      else
	passed = result == RC_FONT_PARTIAL;
    }
  passed = (passed && result == RC_FONT_REFUSED
	    && send_file (store, &file, state) == RC_FONT_REFUSED
	    && count_files (directory) == 0 && !want (store, &file));
  free_file (&file);
  rc_font_store_free (store);
  return passed;
}

static bool
unasked_law (const char *directory, uint64_t *state)
{
  struct rc_font_store *store = rc_font_store_new (directory);
  struct test_file file = random_file (state);
  bool passed = (send_chunk (store, &file, 0,
			     chunk_size (&file, 0, state)) == RC_FONT_REFUSED
		 && count_files (directory) == 0);
  free_file (&file);
  rc_font_store_free (store);
  return passed;
}

static bool
want_law (const char *directory, uint64_t *state)
{
  struct rc_font_store *store = rc_font_store_new (directory);
  struct test_file file = random_file (state);
  struct rc_font_key key = key_of (&file);
  struct rc_font_key short_key = key;
  short_key.nhash = random_below (state, RD_HASH_SIZE);
  struct rc_font_key empty = key, large = key;
  empty.file_size = 0;
  large.file_size = RC_FONT_STORE_LIMIT + 1 + random_below (state, 1000);
  struct rc_font_key limit = key;
  limit.file_size = RC_FONT_STORE_LIMIT;
  limit.hash = (const uint8_t[RD_HASH_SIZE]) { 1, 2, 3 };
  bool passed = (!rc_font_store_want (store, &short_key)
		 && !rc_font_store_want (store, &empty)
		 && !rc_font_store_want (store, &large)
		 && rc_font_store_want (store, &limit)
		 && rc_font_store_want (store, &key)
		 && !rc_font_store_want (store, &key));
  /* Some bytes come, and then the connection ends.  */
  uint32_t size = chunk_size (&file, 0, state);
  passed = passed && (send_chunk (store, &file, 0, size)
		      == (size == file.size ? RC_FONT_DONE : RC_FONT_PARTIAL));
  bool done = size == file.size;
  rc_font_store_reset (store);
  passed = (passed && count_files (directory) == done
	    && rc_font_store_want (store, &key) == !done);
  if (!done)
    passed = (passed && send_file (store, &file, state) == RC_FONT_DONE
	      && has_only (directory, &file));
  passed = passed && !rc_font_store_want (store, &key);
  free_file (&file);
  rc_font_store_free (store);
  return passed;
}

static bool
keep_law (const char *directory, uint64_t *state)
{
  struct rc_font_store *store = rc_font_store_new (directory);
  struct test_file file = random_file (state);
  bool passed = (want (store, &file)
		 && send_file (store, &file, state) == RC_FONT_DONE);
  rc_font_store_free (store);
  store = rc_font_store_new (directory);
  passed = passed && !want (store, &file) && has_only (directory, &file);
  struct rc_font_source source = rc_font_store_source (store);
  struct rc_font_key key = key_of (&file);
  cairo_font_face_t *face = source.find (source.data, &key);
  passed = passed && face;
  if (face)
    cairo_font_face_destroy (face);
  key.nhash = random_below (state, RD_HASH_SIZE);
  face = source.find (source.data, &key);
  passed = passed && !face;
  if (face)
    cairo_font_face_destroy (face);
  rc_font_source_free (source);
  free_file (&file);
  rc_font_store_free (store);
  return passed;
}

/* The face example.  */
static bool
face_example (void)
{
  FcPattern *pattern = FcNameParse ((const FcChar8 *) "monospace");
  FcConfigSubstitute (NULL, pattern, FcMatchPattern);
  FcDefaultSubstitute (pattern);
  FcResult result;
  FcPattern *match = FcFontMatch (NULL, pattern, &result);
  FcPatternDestroy (pattern);
  FcChar8 *name = NULL;
  int index = 0;
  gchar *bytes = NULL;
  gsize size = 0;
  bool passed = (match
		 && FcPatternGetString (match, FC_FILE, 0, &name)
		 == FcResultMatch
		 && FcPatternGetInteger (match, FC_INDEX, 0, &index)
		 == FcResultMatch
		 && g_file_get_contents ((const char *) name, &bytes, &size,
					 NULL));
  char *directory = new_directory ();
  char *fonts = g_build_filename (directory, "fonts", NULL);
  struct rc_font_store *store = rc_font_store_new (fonts);
  struct rc_font_source source = rc_font_store_source (store);
  if (passed)
    {
      struct test_file file = { .bytes = (uint8_t *) bytes, .size = size };
      gsize length = RD_HASH_SIZE;
      GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA256);
      g_checksum_update (checksum, file.bytes, file.size);
      g_checksum_get_digest (checksum, file.hash, &length);
      g_checksum_free (checksum);
      uint64_t state = 1;
      struct rc_font_key key = key_of (&file);
      key.index = index;
      cairo_font_face_t *face = NULL;
      FT_Face ft = NULL;
      passed = (want (store, &file)
		&& send_file (store, &file, &state) == RC_FONT_DONE
		&& (face = source.find (source.data, &key)));
      if (passed)
	{
	  cairo_matrix_t size, identity;
	  cairo_matrix_init_scale (&size, 10, 10);
	  cairo_matrix_init_identity (&identity);
	  cairo_font_options_t *options = cairo_font_options_create ();
	  cairo_scaled_font_t *scaled
	    = cairo_scaled_font_create (face, &size, &identity, options);
	  cairo_font_options_destroy (options);
	  ft = cairo_ft_scaled_font_lock_face (scaled);
	  passed = ft && ft->face_index == index;
	  if (ft)
	    cairo_ft_scaled_font_unlock_face (scaled);
	  cairo_scaled_font_destroy (scaled);
	}
      if (face)
	cairo_font_face_destroy (face);
    }
  if (!passed)
    fprintf (stderr, "test-fontstore: example face: the source gave no "
	     "face of the monospace file %s\n", name ? (char *) name : "");
  rc_font_source_free (source);
  rc_font_store_free (store);
  g_free (fonts);
  remove_directory (directory);
  g_free (bytes);
  if (match)
    FcPatternDestroy (match);
  return passed;
}

/* The unwritable example.  */
static bool
unwritable_example (void)
{
  char *directory = new_directory ();
  char *fonts = g_build_filename (directory, "fonts", NULL);
  struct rc_font_store *store = rc_font_store_new (fonts);
  uint64_t state = 3;
  struct test_file file = random_file (&state);
  g_chmod (fonts, 0500);
  bool passed = (want (store, &file)
		 && send_chunk (store, &file, 0,
				chunk_size (&file, 0, &state))
		 == RC_FONT_REFUSED);
  g_chmod (fonts, 0700);
  passed = passed && count_files (fonts) == 0;
  if (!passed)
    fprintf (stderr, "test-fontstore: example unwritable: the store did "
	     "not refuse the bytes\n");
  free_file (&file);
  rc_font_store_free (store);
  g_free (fonts);
  remove_directory (directory);
  return passed;
}

/* Return a new file of random bytes, from 1 to MAX_TRIAL_FILE bytes.
   Some files have a size near a multiple of RD_MAX_FONT_CHUNK.  */
static struct test_file
random_file (uint64_t *state)
{
  struct test_file file;
  file.size = (random_below (state, 4) == 0
	       ? (1 + random_below (state, 4)) * RD_MAX_FONT_CHUNK
		 - random_below (state, 2)
	       : 1 + random_below (state, MAX_TRIAL_FILE));
  file.bytes = g_malloc (file.size);
  for (uint32_t i = 0; i < file.size; i++)
    file.bytes[i] = random_next (state);
  gsize length = RD_HASH_SIZE;
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA256);
  g_checksum_update (checksum, file.bytes, file.size);
  g_checksum_get_digest (checksum, file.hash, &length);
  g_checksum_free (checksum);
  return file;
}

/* Free the bytes of FILE.  */
static void
free_file (struct test_file *file)
{
  g_free (file->bytes);
}

/* Return the key of the face 0 of FILE.  */
static struct rc_font_key
key_of (const struct test_file *file)
{
  return (struct rc_font_key) { file->hash, RD_HASH_SIZE, 0, file->size };
}

/* Record in STORE that the client needs FILE, and return the result of
   rc_font_store_want.  */
static bool
want (struct rc_font_store *store, const struct test_file *file)
{
  struct rc_font_key key = key_of (file);
  return rc_font_store_want (store, &key);
}

/* Give STORE the SIZE bytes of FILE from OFFSET in one chunk, and
   return the result.  */
static enum rc_font_result
send_chunk (struct rc_font_store *store, const struct test_file *file,
	    uint32_t offset, uint32_t size)
{
  struct rd_message message = { .type = RD_FONT_DATA };
  struct rd_font_data *data = &message.u.font_data;
  data->nhash = RD_HASH_SIZE;
  memcpy (data->hash, file->hash, RD_HASH_SIZE);
  data->offset = offset;
  data->total = file->size;
  data->nbytes = size;
  memcpy (data->bytes, file->bytes + offset, size);
  return rc_font_store_receive (store, data);
}

/* Give STORE all bytes of FILE in random chunks.  Return the result of
   the last chunk, or RC_FONT_REFUSED if a chunk before it does not
   give RC_FONT_PARTIAL.  */
static enum rc_font_result
send_file (struct rc_font_store *store, const struct test_file *file,
	   uint64_t *state)
{
  uint32_t offset = 0;
  for (;;)
    {
      uint32_t size = chunk_size (file, offset, state);
      enum rc_font_result result = send_chunk (store, file, offset, size);
      offset += size;
      if (offset == file->size)
	return result;
      if (result != RC_FONT_PARTIAL)
	return RC_FONT_REFUSED;
    }
}

/* Return a random size of the chunk of FILE at OFFSET: from 1 to
   RD_MAX_FONT_CHUNK bytes, and not past the end of FILE.  Some chunks
   have the largest size.  */
static uint32_t
chunk_size (const struct test_file *file, uint32_t offset, uint64_t *state)
{
  uint32_t left = file->size - offset;
  uint32_t size = (random_below (state, 2) ? RD_MAX_FONT_CHUNK
		   : 1 + random_below (state, RD_MAX_FONT_CHUNK));
  return size < left ? size : left;
}

/* Return true if DIRECTORY has only one file, its name is the hash of
   FILE in hexadecimal, and it has the bytes of FILE.  */
static bool
has_only (const char *directory, const struct test_file *file)
{
  char hex[2 * RD_HASH_SIZE + 1];
  rc_hash_to_hex (file->hash, hex);
  char *name = g_build_filename (directory, hex, NULL);
  gchar *bytes;
  gsize size;
  bool passed = (count_files (directory) == 1
		 && g_file_get_contents (name, &bytes, &size, NULL)
		 && size == file->size
		 && !memcmp (bytes, file->bytes, size));
  if (passed)
    g_free (bytes);
  g_free (name);
  return passed;
}

/* Return the number of files in DIRECTORY, or 0 if it does not
   exist.  */
static int
count_files (const char *directory)
{
  GDir *dir = g_dir_open (directory, 0, NULL);
  int count = 0;
  while (dir && g_dir_read_name (dir))
    count++;
  if (dir)
    g_dir_close (dir);
  return count;
}

/* Return the name of a new empty directory.  */
static char *
new_directory (void)
{
  char *directory = g_strdup ("/tmp/test-fontstore-XXXXXX");
  if (!g_mkdtemp (directory))
    {
      perror ("test-fontstore: mkdtemp");
      exit (1);
    }
  return directory;
}

/* Remove DIRECTORY, its subdirectory "fonts", and their files, and
   free DIRECTORY.  */
static void
remove_directory (char *directory)
{
  char *fonts = g_build_filename (directory, "fonts", NULL);
  GDir *dir = g_dir_open (fonts, 0, NULL);
  const char *name;
  while (dir && (name = g_dir_read_name (dir)))
    {
      char *file = g_build_filename (fonts, name, NULL);
      g_unlink (file);
      g_free (file);
    }
  if (dir)
    g_dir_close (dir);
  g_rmdir (fonts);
  g_rmdir (directory);
  g_free (fonts);
  g_free (directory);
}

/* Return a random number from 0 to LIMIT - 1.  */
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
