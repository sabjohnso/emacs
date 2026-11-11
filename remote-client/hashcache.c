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

/* Each line of the cache file is one entry:

     HASH SIZE SECONDS NANOSECONDS NAME

   HASH is the SHA-256 hash in 64 hexadecimal digits.  SIZE and the
   modification time SECONDS and NANOSECONDS come from stat.  One space
   comes before NAME, and NAME is the rest of the line, so it can have
   spaces, also at its start.  A name with a newline
   has no entry in the file.  The client cannot continue with no
   memory, so a failed allocation stops the program, as in GLib.  */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "hashcache.h"

struct rc_cache_entry
{
  char *name;
  long long size;
  long long seconds;
  long nanoseconds;
  uint8_t hash[RD_HASH_SIZE];
};

/* The cache: its file name or NULL, and its entries, at most one for
   each name.  CHANGED is true if the entries are not the entries of
   the file.  */
struct rc_hash_cache
{
  char *file;
  GArray *entries;
  bool changed;
};

static struct rc_cache_entry *find_entry (const struct rc_hash_cache *,
					  const char *);
static bool entry_valid (const struct rc_cache_entry *,
			 const struct stat *);
static bool parse_line (const char *, struct rc_cache_entry *);
static bool parse_hex (const char *, uint8_t *);
static void append_line (GString *, const struct rc_cache_entry *);

struct rc_hash_cache *
rc_hash_cache_load (const char *name)
{
  struct rc_hash_cache *cache = g_new0 (struct rc_hash_cache, 1);
  cache->file = g_strdup (name);
  cache->entries = g_array_new (false, false,
				sizeof (struct rc_cache_entry));
  gchar *text;
  if (!name || !g_file_get_contents (name, &text, NULL, NULL))
    return cache;
  gchar **lines = g_strsplit (text, "\n", -1);
  for (gchar **line = lines; *line; line++)
    {
      struct rc_cache_entry entry;
      if (parse_line (*line, &entry) && !find_entry (cache, entry.name))
	g_array_append_val (cache->entries, entry);
      else if (entry.name)
	g_free (entry.name);
    }
  g_strfreev (lines);
  g_free (text);
  return cache;
}

void
rc_hash_cache_free (struct rc_hash_cache *cache)
{
  for (guint i = 0; i < cache->entries->len; i++)
    g_free (g_array_index (cache->entries, struct rc_cache_entry, i).name);
  g_array_free (cache->entries, true);
  g_free (cache->file);
  g_free (cache);
}

bool
rc_hash_cache_get (const struct rc_hash_cache *cache, const char *name,
		   const struct stat *st, uint8_t hash[RD_HASH_SIZE])
{
  struct rc_cache_entry *entry = find_entry (cache, name);
  if (!entry || !entry_valid (entry, st))
    return false;
  memcpy (hash, entry->hash, RD_HASH_SIZE);
  return true;
}

void
rc_hash_cache_put (struct rc_hash_cache *cache, const char *name,
		   const struct stat *st, const uint8_t hash[RD_HASH_SIZE])
{
  struct rc_cache_entry *entry = find_entry (cache, name);
  if (!entry)
    {
      struct rc_cache_entry new_entry = { .name = g_strdup (name) };
      g_array_append_val (cache->entries, new_entry);
      entry = &g_array_index (cache->entries, struct rc_cache_entry,
			      cache->entries->len - 1);
    }
  entry->size = st->st_size;
  entry->seconds = st->st_mtim.tv_sec;
  entry->nanoseconds = st->st_mtim.tv_nsec;
  memcpy (entry->hash, hash, RD_HASH_SIZE);
  cache->changed = true;
}

/* Write a new file and rename it, so a reader never sees a part of
   the file.  */
void
rc_hash_cache_save (struct rc_hash_cache *cache)
{
  if (!cache->file || !cache->changed)
    return;
  char *directory = g_path_get_dirname (cache->file);
  g_mkdir_with_parents (directory, 0700);
  g_free (directory);
  GString *text = g_string_new (NULL);
  for (guint i = 0; i < cache->entries->len; i++)
    append_line (text, &g_array_index (cache->entries,
				       struct rc_cache_entry, i));
  cache->changed = !g_file_set_contents (cache->file, text->str,
					 text->len, NULL);
  g_string_free (text, true);
}

void
rc_hash_to_hex (const uint8_t hash[RD_HASH_SIZE],
		char hex[2 * RD_HASH_SIZE + 1])
{
  static const char digits[] = "0123456789abcdef";
  for (int i = 0; i < RD_HASH_SIZE; i++)
    {
      hex[2 * i] = digits[hash[i] >> 4];
      hex[2 * i + 1] = digits[hash[i] & 0xf];
    }
  hex[2 * RD_HASH_SIZE] = '\0';
}

/* Return the entry of NAME in CACHE, or NULL if it has none.  */
static struct rc_cache_entry *
find_entry (const struct rc_hash_cache *cache, const char *name)
{
  for (guint i = 0; i < cache->entries->len; i++)
    {
      struct rc_cache_entry *entry = &g_array_index (cache->entries,
						     struct rc_cache_entry,
						     i);
      if (!strcmp (entry->name, name))
	return entry;
    }
  return NULL;
}

/* Return true if ENTRY is valid for a file with the status ST.  */
static bool
entry_valid (const struct rc_cache_entry *entry, const struct stat *st)
{
  return (entry->size == st->st_size
	  && entry->seconds == st->st_mtim.tv_sec
	  && entry->nanoseconds == st->st_mtim.tv_nsec);
}

/* Set *ENTRY to the entry of the cache file line LINE, and return
   true.  Return false if LINE does not have the form of an entry.  The
   name of *ENTRY is a new string, or NULL.  */
static bool
parse_line (const char *line, struct rc_cache_entry *entry)
{
  char hex[2 * RD_HASH_SIZE + 1];
  int end;
  entry->name = NULL;
  if (sscanf (line, "%64s %lld %lld %ld%n", hex, &entry->size,
	      &entry->seconds, &entry->nanoseconds, &end) != 4
      || line[end] != ' ' || line[end + 1] == '\0'
      || !parse_hex (hex, entry->hash))
    return false;
  entry->name = g_strdup (line + end + 1);
  return true;
}

/* Set HASH to the bytes of the 2 * RD_HASH_SIZE hexadecimal digits HEX,
   and return true.  Return false if HEX is not such digits.  */
static bool
parse_hex (const char *hex, uint8_t *hash)
{
  if (strlen (hex) != 2 * RD_HASH_SIZE)
    return false;
  for (int i = 0; i < RD_HASH_SIZE; i++)
    {
      int high = g_ascii_xdigit_value (hex[2 * i]);
      int low = g_ascii_xdigit_value (hex[2 * i + 1]);
      if (high < 0 || low < 0)
	return false;
      hash[i] = high << 4 | low;
    }
  return true;
}

/* Append the line of ENTRY to TEXT.  A name with a newline has no
   line.  */
static void
append_line (GString *text, const struct rc_cache_entry *entry)
{
  if (strchr (entry->name, '\n'))
    return;
  char hex[2 * RD_HASH_SIZE + 1];
  rc_hash_to_hex (entry->hash, hex);
  g_string_append_printf (text, "%s %lld %lld %ld %s\n", hex, entry->size,
			  entry->seconds, entry->nanoseconds, entry->name);
}
