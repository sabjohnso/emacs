/* Tests of the font sources of the remote display client.

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

/* The test copies some font files of the system to a new directory:
   files with one face, and one file with more than one face.  The
   property runs hash sources on random lists of these files, with
   random requests, and compares them with a model.  The laws:

   - found: for the SHA-256 hash of a file of the list and a face
     index of the file, the source gives that face of that file.
   - missing: for a hash of no file of the list, or a hash with fewer
     than RD_HASH_SIZE bytes, the source gives NULL.
   - lazy: the source hashes each file at most one time, in the order
     of the list, and only when a request needs it.  A request that
     finds a file hashes the files before it.  A request that finds no
     file hashes all files.
   - cache: a source with a cache file hashes no file that an earlier
     source with that cache file hashed, if the file did not change,
     also with another list of files.  It hashes a file again after a
     change of its size or of its modification time.  A session that
     hashes no file does not write the cache file.  A cache file with
     bad lines changes no result.  A name with a newline has no cache
     entry, and a name with spaces has one, also a relative name with
     a space at its start.
   - unreadable: a file that does not exist, or that the hash function
     cannot read, matches no hash, and the source tries it only one
     time.  The files after it can match.
   - chain: a chain of two sources gives the face of the first source,
     and does not ask the second.  If the first gives NULL, the chain
     asks the second with the same arguments, gives its face or NULL,
     and calls its warning function one time with the arguments.  The
     chain frees both sources.
   - file names: the font file names of fontconfig have no duplicate,
     and they have the file of the match of "monospace".

   With no argument, the program runs the examples and TRIALS trials
   from a seed that it gets from the clock and the process ID.  With
   the argument SEED, it runs only the trial of that seed.  */

#include <config.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "fonts.h"
#include "hashfonts.h"
#include "rdproto.h"

enum { TRIALS = 40, STEPS = 30, SESSIONS = 3, MAX_FILES = 8 };

/* The largest size of a test file with one face.  */
enum { SMALL_FILE = 2000000 };

/* A test file: its name, the number of its faces, and its hash.  */
struct test_file
{
  char *name;
  int faces;
  uint8_t hash[RD_HASH_SIZE];
};

/* The identity of a face: the values that FreeType gives for it.  */
struct face_id
{
  char family[64];
  char style[64];
  long glyphs;
};

/* The data of the hash function of the test: the number of its calls,
   and the name of a file that it cannot read, or NULL.  */
struct counter
{
  int calls;
  const char *unreadable;
};

static struct test_file files[MAX_FILES];
static int nfiles;
static char directory[] = "/tmp/test-fonts-XXXXXX";

static bool find_test_files (void);
static bool copy_test_file (const char *, int);
static bool run_examples (void);
static bool stale_example (void);
static bool warm_session (const char *const *, int, const char *,
			  const struct test_file *);
static bool bad_cache_example (void);
static bool unchanged_cache_example (void);
static bool unreadable_example (void);
static bool names_example (void);
static bool leading_space_case (const char *);
static bool chain_example (void);
static bool chain_case (bool, bool);
static bool file_names_example (void);
static bool run_trial (uint64_t);
static bool run_session (const char *, bool *, uint64_t *);
static bool check_request (struct rc_font_source, const uint8_t *, int,
			   int, const struct test_file *);
static struct rc_font_source new_source (const char *const *, int,
					 const char *, struct counter *);
static bool count_hash (void *, const char *, uint8_t *);
static bool file_hash (const char *, uint8_t *);
static const uint8_t *missing_hash (void);
static bool copy_file (const char *, const char *);
static void set_time (const char *, struct timespec);
static bool face_identity (cairo_font_face_t *, struct face_id *);
static bool file_identity (const char *, int, struct face_id *);
static void remove_directory (void);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

int
main (int argc, char **argv)
{
  if (!mkdtemp (directory) || !find_test_files ())
    {
      fprintf (stderr, "test-fonts: no test font files\n");
      remove_directory ();
      return 1;
    }
  bool passed;
  if (argc == 2)
    passed = run_trial (strtoull (argv[1], NULL, 10));
  else
    {
      passed = run_examples ();
      uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
      for (int trial = 0; passed && trial < TRIALS; trial++)
	passed = run_trial (base + trial);
      if (passed)
	printf ("test-fonts: the examples and %d trials passed\n", TRIALS);
    }
  remove_directory ();
  return passed ? 0 : 1;
}

/* The test files.  */

/* Copy up to MAX_FILES - 1 small font files with one face, and the
   smallest file with more than one face, to the test directory.
   Return true if the files have at least two families.  */
static bool
find_test_files (void)
{
  FcObjectSet *objects = FcObjectSetBuild (FC_FILE, FC_INDEX, NULL);
  FcPattern *all = FcPatternCreate ();
  FcFontSet *set = FcFontList (NULL, all, objects);
  FcPatternDestroy (all);
  FcObjectSetDestroy (objects);
  const char *collection = NULL;
  int collection_faces = 0;
  off_t collection_size = 0;
  for (int i = 0; set && i < set->nfont; i++)
    {
      FcChar8 *name;
      int index;
      struct stat st;
      if (FcPatternGetString (set->fonts[i], FC_FILE, 0, &name)
	  != FcResultMatch
	  || FcPatternGetInteger (set->fonts[i], FC_INDEX, 0, &index)
	  != FcResultMatch
	  || stat ((const char *) name, &st) != 0)
	continue;
      FT_Library library;
      FT_Face face;
      FT_Init_FreeType (&library);
      long faces = (FT_New_Face (library, (const char *) name, -1, &face)
		    ? 0 : face->num_faces);
      if (faces)
	FT_Done_Face (face);
      FT_Done_FreeType (library);
      if (faces > 1 && index == 0
	  && (!collection || st.st_size < collection_size))
	{
	  collection = (const char *) name;
	  collection_faces = faces;
	  collection_size = st.st_size;
	}
      else if (faces == 1 && st.st_size < SMALL_FILE
	       && nfiles < MAX_FILES - 1)
	copy_test_file ((const char *) name, 1);
    }
  if (collection)
    copy_test_file (collection, collection_faces);
  FcFontSetDestroy (set);
  return nfiles >= 2;
}

/* Copy the font file NAME with FACES faces to the test directory, and
   add it to the test files.  Skip a file with the family and the style
   of a test file.  */
static bool
copy_test_file (const char *name, int faces)
{
  struct face_id id;
  if (!file_identity (name, 0, &id))
    return false;
  for (int i = 0; i < nfiles; i++)
    {
      struct face_id other;
      file_identity (files[i].name, 0, &other);
      if (!strcmp (id.family, other.family) && !strcmp (id.style, other.style))
	return false;
    }
  char *copy = g_strdup_printf ("%s/%d-%s", directory, nfiles,
				strrchr (name, '/') + 1);
  if (!copy_file (name, copy))
    {
      g_free (copy);
      return false;
    }
  files[nfiles] = (struct test_file) { .name = copy, .faces = faces };
  file_hash (copy, files[nfiles].hash);
  nfiles++;
  return true;
}

/* The examples.  */

static bool
run_examples (void)
{
  bool stale = stale_example ();
  bool bad_cache = bad_cache_example ();
  bool unchanged = unchanged_cache_example ();
  bool unreadable = unreadable_example ();
  bool names = names_example ();
  bool chain = chain_example ();
  bool file_names = file_names_example ();
  return (stale && bad_cache && unchanged && unreadable && names && chain
	  && file_names);
}

/* The cache law for a file that changes, in three steps.  A copy of
   the test file A first gets the content of the test file B, which has
   another size, with the old modification time: only the size shows
   the change.  Then one byte changes, and the seconds of the time
   change.  Then the byte is again the byte of B, and only the
   nanoseconds of the time change.  After the second and the last
   step, a session with no change hashes no file, so the new entries
   are in the cache file.  The list has an unchanged test file C before
   the copy, which the source hashes only one time.  Cairo keeps the
   face of a file name, and would give an old face for new content, so
   no step loads a face of other content.  */
static bool
stale_example (void)
{
  int a = 0, b = 1;
  struct stat sa, sb;
  while (b < nfiles && stat (files[a].name, &sa) == 0
	 && stat (files[b].name, &sb) == 0 && sa.st_size == sb.st_size)
    b++;
  int c = 1;
  while (c == a || c == b)
    c++;
  if (b == nfiles || c == nfiles)
    {
      fprintf (stderr, "test-fonts: example stale: no three test files, "
	       "two of other sizes\n");
      return false;
    }
  char *copy = g_strdup_printf ("%s/stale", directory);
  char *cache = g_strdup_printf ("%s/stale-cache", directory);
  const char *list[] = { files[c].name, copy };
  enum { N = sizeof list / sizeof *list };
  struct counter counter = { 0 };
  copy_file (files[a].name, copy);
  struct stat old;
  stat (copy, &old);
  uint8_t missing[RD_HASH_SIZE];
  memcpy (missing, files[a].hash, RD_HASH_SIZE);
  missing[0] ^= 1;

  struct rc_font_source source = new_source (list, N, cache, &counter);
  bool passed = (check_request (source, missing, RD_HASH_SIZE, 0, NULL)
		 && counter.calls == 2);
  rc_font_source_free (source);

  copy_file (files[b].name, copy);
  set_time (copy, old.st_mtim);
  source = new_source (list, N, cache, &counter);
  passed = (passed
	    && check_request (source, files[b].hash, RD_HASH_SIZE, 0,
			      &files[b])
	    && check_request (source, files[a].hash, RD_HASH_SIZE, 0, NULL)
	    && counter.calls == 1);
  rc_font_source_free (source);
  passed = passed && warm_session (list, N, cache, &files[b]);

  gchar *bytes;
  gsize size;
  g_file_get_contents (copy, &bytes, &size, NULL);
  bytes[size / 2] ^= 1;
  g_file_set_contents (copy, bytes, size, NULL);
  struct timespec time = { old.st_mtim.tv_sec + 1, old.st_mtim.tv_nsec };
  set_time (copy, time);
  source = new_source (list, N, cache, &counter);
  passed = (passed
	    && check_request (source, files[b].hash, RD_HASH_SIZE, 0, NULL)
	    && counter.calls == 1);
  rc_font_source_free (source);

  bytes[size / 2] ^= 1;
  g_file_set_contents (copy, bytes, size, NULL);
  g_free (bytes);
  time.tv_nsec = (time.tv_nsec + 1) % 1000000000;
  set_time (copy, time);
  source = new_source (list, N, cache, &counter);
  passed = (passed
	    && check_request (source, files[b].hash, RD_HASH_SIZE, 0,
			      &files[b])
	    && counter.calls == 1);
  rc_font_source_free (source);
  passed = passed && warm_session (list, N, cache, &files[b]);
  unlink (copy);
  unlink (cache);
  g_free (copy);
  g_free (cache);
  if (!passed)
    fprintf (stderr, "test-fonts: example stale: a changed file did not "
	     "get a new hash\n");
  return passed;
}

/* Return true if a session with the N files LIST and the cache file
   CACHE finds FILE with no hash, and finds no file for a missing hash
   with no hash.  */
static bool
warm_session (const char *const *list, int n, const char *cache,
	      const struct test_file *file)
{
  struct counter counter = { 0 };
  struct rc_font_source source = new_source (list, n, cache, &counter);
  bool passed = (check_request (source, file->hash, RD_HASH_SIZE, 0, file)
		 && check_request (source, missing_hash (), RD_HASH_SIZE, 0,
				   NULL)
		 && counter.calls == 0);
  rc_font_source_free (source);
  return passed;
}

/* The cache law for bad lines: a cache file of bad lines changes no
   result, and the source hashes each file as with no cache.  */
static bool
bad_cache_example (void)
{
  char *cache = g_strdup_printf ("%s/bad-cache", directory);
  static const char bad[] = "xyz\n\n0 0 0 0\nzz 1 2 3 /nothing\n"
    "0123456789012345678901234567890123456789012345678901234567890123"
    " 1 2 3\n"
    "012345678901234567890123456789012345678901234567890123456789012g"
    " 1 2 3 /x\n";
  g_file_set_contents (cache, bad, sizeof bad - 1, NULL);
  const char *list[] = { files[0].name, files[1].name };
  struct counter counter = { 0 };
  struct rc_font_source source = new_source (list, 2, cache, &counter);
  bool passed = (check_request (source, files[1].hash, RD_HASH_SIZE, 0,
				&files[1])
		 && counter.calls == 2);
  rc_font_source_free (source);
  unlink (cache);
  g_free (cache);
  if (!passed)
    fprintf (stderr, "test-fonts: example bad cache: the result or the "
	     "number of hashes is not correct\n");
  return passed;
}

/* The cache law for a session that hashes no file: the cache file
   keeps its modification time.  */
static bool
unchanged_cache_example (void)
{
  char *cache = g_strdup_printf ("%s/unchanged-cache", directory);
  const char *list[] = { files[0].name };
  struct counter counter = { 0 };
  struct rc_font_source source = new_source (list, 1, cache, &counter);
  bool passed = check_request (source, files[0].hash, RD_HASH_SIZE, 0,
			       &files[0]);
  rc_font_source_free (source);
  struct timespec early = { 1, 0 };
  set_time (cache, early);
  source = new_source (list, 1, cache, &counter);
  passed = (passed
	    && check_request (source, files[0].hash, RD_HASH_SIZE, 0,
			      &files[0])
	    && counter.calls == 0);
  rc_font_source_free (source);
  struct stat st;
  passed = passed && stat (cache, &st) == 0 && st.st_mtim.tv_sec == 1;
  unlink (cache);
  g_free (cache);
  if (!passed)
    fprintf (stderr, "test-fonts: example unchanged cache: a session "
	     "with no hash wrote the cache file\n");
  return passed;
}

/* The unreadable law, for a file that does not exist, and for a file
   that the hash function cannot read.  */
static bool
unreadable_example (void)
{
  char *missing = g_strdup_printf ("%s/missing", directory);
  const char *list[] = { missing, files[1].name };
  struct counter counter = { 0 };
  struct rc_font_source source = new_source (list, 2, NULL, &counter);
  bool passed = (check_request (source, files[1].hash, RD_HASH_SIZE, 0,
				&files[1])
		 && counter.calls == 1);
  rc_font_source_free (source);
  g_free (missing);

  const char *both[] = { files[0].name, files[1].name };
  counter = (struct counter) { .unreadable = files[0].name };
  source = new_source (both, 2, NULL, &counter);
  passed = (passed
	    && check_request (source, files[0].hash, RD_HASH_SIZE, 0, NULL)
	    && check_request (source, files[1].hash, RD_HASH_SIZE, 0,
			      &files[1])
	    && check_request (source, files[0].hash, RD_HASH_SIZE, 0, NULL)
	    && counter.calls == 2);
  rc_font_source_free (source);
  if (!passed)
    fprintf (stderr, "test-fonts: example unreadable: the result or the "
	     "number of hashes is not correct\n");
  return passed;
}

/* The cache law for names with spaces and a newline.  If the cache
   file had a line for the name with a newline, the first part of the
   name would be a false entry for the file "new".  That file has the
   size and the modification time of the file with the newline, and
   other content.  */
static bool
names_example (void)
{
  char *spaces = g_strdup_printf ("%s/a b  c.ttf", directory);
  char *newline = g_strdup_printf ("%s/new\nline.ttf", directory);
  char *first_part = g_strdup_printf ("%s/new", directory);
  char *cache = g_strdup_printf ("%s/names-cache", directory);
  copy_file (files[0].name, spaces);
  copy_file (files[1].name, newline);
  const char *list[] = { spaces, newline };
  struct counter counter = { 0 };
  struct rc_font_source source = new_source (list, 2, cache, &counter);
  bool passed = (check_request (source, missing_hash (), RD_HASH_SIZE, 0,
				NULL)
		 && counter.calls == 2);
  rc_font_source_free (source);
  source = new_source (list, 2, cache, &counter);
  passed = (passed
	    && check_request (source, files[0].hash, RD_HASH_SIZE, 0,
			      &files[0])
	    && counter.calls == 0
	    && check_request (source, files[1].hash, RD_HASH_SIZE, 0,
			      &files[1])
	    && counter.calls == 1);
  rc_font_source_free (source);
  passed = passed && leading_space_case (cache);

  gchar *bytes;
  gsize size;
  g_file_get_contents (newline, &bytes, &size, NULL);
  bytes[size / 2] ^= 1;
  g_file_set_contents (first_part, bytes, size, NULL);
  g_free (bytes);
  struct stat st;
  stat (newline, &st);
  set_time (first_part, st.st_mtim);
  const char *false_list[] = { first_part };
  source = new_source (false_list, 1, cache, &counter);
  passed = (passed
	    && check_request (source, files[1].hash, RD_HASH_SIZE, 0, NULL)
	    && counter.calls == 1);
  rc_font_source_free (source);
  unlink (spaces);
  unlink (newline);
  unlink (first_part);
  unlink (cache);
  g_free (spaces);
  g_free (newline);
  g_free (first_part);
  g_free (cache);
  if (!passed)
    fprintf (stderr, "test-fonts: example names: the cache of a name with "
	     "spaces or a newline is not correct\n");
  return passed;
}

/* The cache law for the relative names " lead.ttf" and "lead.ttf" in
   the test directory, with the cache file CACHE.  The two files have
   the same size and modification time, and other content.  If the
   space at the start of a name were lost, the entry of " lead.ttf"
   would be a false entry for "lead.ttf".  */
static bool
leading_space_case (const char *cache)
{
  char *old = g_get_current_dir ();
  if (chdir (directory) != 0)
    return false;
  gchar *bytes;
  gsize size;
  g_file_get_contents (files[1].name, &bytes, &size, NULL);
  g_file_set_contents (" lead.ttf", bytes, size, NULL);
  bytes[size / 2] ^= 1;
  g_file_set_contents ("lead.ttf", bytes, size, NULL);
  g_free (bytes);
  struct stat st;
  stat (" lead.ttf", &st);
  set_time ("lead.ttf", st.st_mtim);
  const char *first[] = { " lead.ttf" };
  const char *second[] = { "lead.ttf" };
  struct counter counter = { 0 };
  struct rc_font_source source = new_source (first, 1, cache, &counter);
  bool passed = (check_request (source, missing_hash (), RD_HASH_SIZE, 0,
				NULL)
		 && counter.calls == 1);
  rc_font_source_free (source);
  source = new_source (second, 1, cache, &counter);
  passed = (passed
	    && check_request (source, files[1].hash, RD_HASH_SIZE, 0, NULL)
	    && counter.calls == 1);
  rc_font_source_free (source);
  unlink (" lead.ttf");
  unlink ("lead.ttf");
  passed = chdir (old) == 0 && passed;
  g_free (old);
  return passed;
}

/* The data of a font source of the chain example: the face that it
   gives, the number of calls of FIND and DESTROY, and the arguments of
   the last call of FIND.  */
struct fixed_source
{
  cairo_font_face_t *face;
  int finds;
  int destroyed;
  uint8_t first_byte;
  int nhash;
  uint32_t index;
};

static cairo_font_face_t *
find_fixed (void *data, const uint8_t *hash, int nhash, uint32_t index)
{
  struct fixed_source *source = data;
  source->finds++;
  source->first_byte = hash[0];
  source->nhash = nhash;
  source->index = index;
  return source->face ? cairo_font_face_reference (source->face) : NULL;
}

static void
destroy_fixed (void *data)
{
  struct fixed_source *source = data;
  source->destroyed++;
}

/* The calls of record_warning, and the arguments of the last call.  */
static struct fixed_source warnings;

static void
record_warning (const uint8_t *hash, int nhash, uint32_t index)
{
  warnings.finds++;
  warnings.first_byte = hash[0];
  warnings.nhash = nhash;
  warnings.index = index;
}

/* The chain law, for each case of found and not found.  */
static bool
chain_example (void)
{
  bool passed = true;
  for (int i = 0; i < 4; i++)
    passed &= chain_case (i & 1, i & 2);
  if (!passed)
    fprintf (stderr, "test-fonts: example chain: the face, the calls, "
	     "the arguments, or the frees are not correct\n");
  return passed;
}

/* Return true if the chain law holds for a first source that finds a
   face if FIRST_FINDS, and a second source that finds a face if
   SECOND_FINDS.  */
static bool
chain_case (bool first_finds, bool second_finds)
{
  cairo_font_face_t *a = cairo_toy_font_face_create ("a", 0, 0);
  cairo_font_face_t *b = cairo_toy_font_face_create ("b", 0, 0);
  struct fixed_source first = { .face = first_finds ? a : NULL };
  struct fixed_source second = { .face = second_finds ? b : NULL };
  warnings = (struct fixed_source) { 0 };
  uint8_t hash[RD_HASH_SIZE] = { 0x5a };
  struct rc_font_source chain = rc_font_source_chain
    ((struct rc_font_source) { find_fixed, destroy_fixed, &first },
     (struct rc_font_source) { find_fixed, destroy_fixed, &second },
     record_warning);
  cairo_font_face_t *found = chain.find (chain.data, hash, 17, 9);
  cairo_font_face_t *expected = (first_finds ? a : second_finds ? b
				 : NULL);
  bool passed = (found == expected && first.finds == 1
		 && second.finds == !first_finds
		 && warnings.finds == !first_finds
		 && first.first_byte == 0x5a && first.nhash == 17
		 && first.index == 9);
  if (!first_finds)
    passed = (passed && second.first_byte == 0x5a && second.nhash == 17
	      && second.index == 9 && warnings.first_byte == 0x5a
	      && warnings.nhash == 17 && warnings.index == 9);
  if (found)
    cairo_font_face_destroy (found);
  rc_font_source_free (chain);
  passed = passed && first.destroyed == 1 && second.destroyed == 1;
  cairo_font_face_destroy (a);
  cairo_font_face_destroy (b);
  return passed;
}

/* The file names law.  */
static bool
file_names_example (void)
{
  char **names = rc_font_file_names ();
  FcPattern *pattern = FcNameParse ((const FcChar8 *) "monospace");
  FcConfigSubstitute (NULL, pattern, FcMatchPattern);
  FcDefaultSubstitute (pattern);
  FcResult result;
  FcPattern *match = FcFontMatch (NULL, pattern, &result);
  FcChar8 *monospace = NULL;
  if (match)
    FcPatternGetString (match, FC_FILE, 0, &monospace);
  GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);
  bool unique = true, found = false;
  for (char **name = names; *name; name++)
    {
      unique &= g_hash_table_add (seen, *name);
      found |= monospace && !strcmp (*name, (const char *) monospace);
    }
  g_hash_table_destroy (seen);
  g_strfreev (names);
  if (match)
    FcPatternDestroy (match);
  FcPatternDestroy (pattern);
  if (!unique || !found)
    fprintf (stderr, "test-fonts: example file names: a name repeats, or "
	     "the monospace file is missing\n");
  return unique && found;
}

/* The property.  */

/* Run the trial of SEED: SESSIONS sessions of random requests, each
   with a new random list of the test files, and one cache file.
   Return true if the trial passes.  */
static bool
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  char *cache = g_strdup_printf ("%s/cache-%llu", directory,
				 (unsigned long long) seed);
  bool hashed[MAX_FILES] = { false };
  bool passed = true;
  for (int session = 0; passed && session < SESSIONS; session++)
    passed = run_session (cache, hashed, &state);
  if (!passed)
    fprintf (stderr, "test-fonts: seed %llu failed\n",
	     (unsigned long long) seed);
  unlink (cache);
  g_free (cache);
  return passed;
}

/* Run a session of random requests with a hash source of a random list
   of the test files, with duplicates, and the cache file CACHE.
   HASHED tells which test files a source with CACHE hashed, and this
   function updates it.  Return true if each request and the number of
   hashes are correct.  */
static bool
run_session (const char *cache, bool *hashed, uint64_t *state)
{
  const char *list[2 * MAX_FILES];
  int n = 1 + random_below (state, 2 * MAX_FILES);
  for (int i = 0; i < n; i++)
    list[i] = files[random_below (state, nfiles)].name;
  struct counter counter = { 0 };
  struct rc_font_source source = new_source (list, n, cache, &counter);
  int expected_calls = 0;
  /* The list positions before END are hashed in this session.  */
  int end = 0;
  bool passed = true;
  for (int step = 0; passed && step < STEPS; step++)
    {
      int kind = random_below (state, 8);
      const struct test_file *file = &files[random_below (state, nfiles)];
      uint8_t hash[RD_HASH_SIZE];
      memcpy (hash, file->hash, RD_HASH_SIZE);
      int nhash = RD_HASH_SIZE;
      if (kind == 0)
	hash[random_below (state, RD_HASH_SIZE)] ^= 1;
      else if (kind == 1)
	nhash = random_below (state, RD_HASH_SIZE);
      int found_at = -1;
      for (int i = 0; kind > 1 && found_at < 0 && i < n; i++)
	if (list[i] == file->name)
	  found_at = i;
      int new_end = (kind == 1 ? end
		     : found_at < 0 ? n
		     : found_at + 1 > end ? found_at + 1 : end);
      for (int i = end; i < new_end; i++)
	{
	  bool seen = false;
	  for (int j = 0; j < i; j++)
	    seen |= list[j] == list[i];
	  int test_index = 0;
	  while (files[test_index].name != list[i])
	    test_index++;
	  if (!seen && !hashed[test_index])
	    expected_calls++;
	}
      end = new_end;
      int index = random_below (state, file->faces);
      passed = check_request (source, hash, nhash, index,
			      found_at >= 0 ? file : NULL);
      if (passed && counter.calls != expected_calls)
	{
	  fprintf (stderr, "test-fonts: %d hashes, not %d\n", counter.calls,
		   expected_calls);
	  passed = false;
	}
    }
  for (int i = 0; i < end; i++)
    for (int j = 0; j < nfiles; j++)
      if (files[j].name == list[i])
	hashed[j] = true;
  rc_font_source_free (source);
  return passed;
}

/* Return true if SOURCE gives the face INDEX of FILE for the NHASH
   bytes HASH, or NULL if FILE is NULL.  */
static bool
check_request (struct rc_font_source source, const uint8_t *hash,
	       int nhash, int index, const struct test_file *file)
{
  cairo_font_face_t *face = source.find (source.data, hash, nhash, index);
  if (!file || !face)
    {
      if (face)
	cairo_font_face_destroy (face);
      if (!file == !face)
	return true;
      fprintf (stderr, "test-fonts: the source gave %s face\n",
	       face ? "a" : "no");
      return false;
    }
  struct face_id expected, actual;
  bool same = (file_identity (file->name, index, &expected)
	       && face_identity (face, &actual)
	       && !strcmp (expected.family, actual.family)
	       && !strcmp (expected.style, actual.style)
	       && expected.glyphs == actual.glyphs);
  cairo_font_face_destroy (face);
  if (!same)
    fprintf (stderr, "test-fonts: the face is not the face %d of %s\n",
	     index, file->name);
  return same;
}

/* Return a new hash source of the N files LIST, with the cache file
   CACHE, and with count_hash and COUNTER as its hash function.  Set
   the calls of COUNTER to 0.  */
static struct rc_font_source
new_source (const char *const *list, int n, const char *cache,
	    struct counter *counter)
{
  counter->calls = 0;
  return rc_hash_font_source_new (list, n, cache, count_hash, counter);
}

/* The hash function of the sources of the test.  DATA is a struct
   counter.  Count the calls, and hash the file NAME with
   rc_sha256_file, if it is not the unreadable file.  */
static bool
count_hash (void *data, const char *name, uint8_t *hash)
{
  struct counter *counter = data;
  counter->calls++;
  return (!(counter->unreadable && !strcmp (name, counter->unreadable))
	  && rc_sha256_file (NULL, name, hash));
}

/* Set HASH to the SHA-256 hash of the file NAME, from all its bytes in
   one buffer.  */
static bool
file_hash (const char *name, uint8_t *hash)
{
  gchar *bytes;
  gsize size;
  if (!g_file_get_contents (name, &bytes, &size, NULL))
    return false;
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA256);
  g_checksum_update (checksum, (const guchar *) bytes, size);
  gsize length = RD_HASH_SIZE;
  g_checksum_get_digest (checksum, hash, &length);
  g_checksum_free (checksum);
  g_free (bytes);
  return true;
}

/* Return a hash of no test file.  */
static const uint8_t *
missing_hash (void)
{
  static uint8_t hash[RD_HASH_SIZE];
  memcpy (hash, files[0].hash, RD_HASH_SIZE);
  hash[0] ^= 1;
  return hash;
}

/* Copy the file FROM to the file TO.  */
static bool
copy_file (const char *from, const char *to)
{
  gchar *bytes;
  gsize size;
  if (!g_file_get_contents (from, &bytes, &size, NULL))
    return false;
  bool copied = g_file_set_contents (to, bytes, size, NULL);
  g_free (bytes);
  return copied;
}

/* Set the access and modification times of the file NAME to TIME.  */
static void
set_time (const char *name, struct timespec time)
{
  struct timespec times[2] = { time, time };
  utimensat (AT_FDCWD, name, times, 0);
}

/* Set *ID to the identity of the Cairo font face FACE.  */
static bool
face_identity (cairo_font_face_t *face, struct face_id *id)
{
  cairo_matrix_t size, identity;
  cairo_matrix_init_scale (&size, 12, 12);
  cairo_matrix_init_identity (&identity);
  cairo_font_options_t *options = cairo_font_options_create ();
  cairo_scaled_font_t *font = cairo_scaled_font_create (face, &size,
							&identity, options);
  cairo_font_options_destroy (options);
  FT_Face ft = cairo_ft_scaled_font_lock_face (font);
  bool found = ft != NULL;
  if (found)
    {
      snprintf (id->family, sizeof id->family, "%s",
		ft->family_name ? ft->family_name : "");
      snprintf (id->style, sizeof id->style, "%s",
		ft->style_name ? ft->style_name : "");
      id->glyphs = ft->num_glyphs;
      cairo_ft_scaled_font_unlock_face (font);
    }
  cairo_scaled_font_destroy (font);
  return found;
}

/* Set *ID to the identity of the face INDEX of the font file NAME, from
   FreeType.  */
static bool
file_identity (const char *name, int index, struct face_id *id)
{
  FT_Library library;
  FT_Face face;
  FT_Init_FreeType (&library);
  bool found = FT_New_Face (library, name, index, &face) == 0;
  if (found)
    {
      snprintf (id->family, sizeof id->family, "%s",
		face->family_name ? face->family_name : "");
      snprintf (id->style, sizeof id->style, "%s",
		face->style_name ? face->style_name : "");
      id->glyphs = face->num_glyphs;
      FT_Done_Face (face);
    }
  FT_Done_FreeType (library);
  return found;
}

/* Remove the test files and the test directory.  */
static void
remove_directory (void)
{
  for (int i = 0; i < nfiles; i++)
    {
      unlink (files[i].name);
      g_free (files[i].name);
    }
  rmdir (directory);
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
