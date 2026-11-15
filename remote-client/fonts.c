/* The font sources of the remote display client.

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

#include <config.h>

#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <glib.h>

#include "fonts.h"

/* The data of a chain.  */
struct chain
{
  struct rc_font_source first;
  struct rc_font_source second;
  rc_fallback_function *warn;
};

static cairo_font_face_t *find_in_chain (void *,
					 const struct rc_font_key *);
static void destroy_chain (void *);
static cairo_font_face_t *find_unhashed (void *,
					 const struct rc_font_key *);
static void destroy_unhashed (void *);
static cairo_font_face_t *find_fallback (void *,
					 const struct rc_font_key *);
static void destroy_fallback (void *);
static cairo_font_face_t *match_face (const char *);

struct rc_font_source
rc_fallback_font_source_new (void)
{
  return (struct rc_font_source) { find_fallback, destroy_fallback,
				   match_face ("monospace") };
}

struct rc_font_source
rc_font_source_chain (struct rc_font_source first,
		      struct rc_font_source second, rc_fallback_function *warn)
{
  struct chain *chain = g_new (struct chain, 1);
  *chain = (struct chain) { first, second, warn };
  return (struct rc_font_source) { find_in_chain, destroy_chain, chain };
}

/* The FIND function of a chain.  DATA is the struct chain.  */
static cairo_font_face_t *
find_in_chain (void *data, const struct rc_font_key *key)
{
  struct chain *chain = data;
  cairo_font_face_t *face = chain->first.find (chain->first.data, key);
  if (face)
    return face;
  if (chain->warn)
    chain->warn (key);
  return chain->second.find (chain->second.data, key);
}

struct rc_font_source
rc_unhashed_font_source (struct rc_font_source inner)
{
  struct rc_font_source *data = g_new (struct rc_font_source, 1);
  *data = inner;
  return (struct rc_font_source) { find_unhashed, destroy_unhashed, data };
}

/* The FIND function of an unhashed source.  DATA is the inner
   source.  */
static cairo_font_face_t *
find_unhashed (void *data, const struct rc_font_key *key)
{
  struct rc_font_source *inner = data;
  return (key->nhash < RD_HASH_SIZE ? inner->find (inner->data, key)
	  : NULL);
}

/* The DESTROY function of an unhashed source.  DATA is the inner
   source.  */
static void
destroy_unhashed (void *data)
{
  rc_font_source_free (*(struct rc_font_source *) data);
  g_free (data);
}

/* The DESTROY function of a chain.  DATA is the struct chain.  */
static void
destroy_chain (void *data)
{
  struct chain *chain = data;
  rc_font_source_free (chain->first);
  rc_font_source_free (chain->second);
  g_free (chain);
}

/* The FIND function of the fallback source.  DATA is the face, or
   NULL.  */
static cairo_font_face_t *
find_fallback (void *data, const struct rc_font_key *key)
{
  (void) key;
  cairo_font_face_t *face = data;
  return face ? cairo_font_face_reference (face) : NULL;
}

/* The DESTROY function of the fallback source.  DATA is the face, or
   NULL.  */
static void
destroy_fallback (void *data)
{
  cairo_font_face_destroy (data);
}

cairo_font_face_t *
rc_file_face (const char *name, uint32_t index)
{
  FcPattern *pattern = FcPatternBuild (NULL, FC_FILE, FcTypeString, name,
				       FC_INDEX, FcTypeInteger, (int) index,
				       NULL);
  FcDefaultSubstitute (pattern);
  cairo_font_face_t *face = cairo_ft_font_face_create_for_pattern (pattern);
  FcPatternDestroy (pattern);
  return face;
}

/* Return a new reference to the face of the fontconfig match of the
   font name NAME, or NULL if fontconfig has no match.  */
static cairo_font_face_t *
match_face (const char *name)
{
  FcPattern *pattern = FcNameParse ((const FcChar8 *) name);
  if (!pattern)
    return NULL;
  FcConfigSubstitute (NULL, pattern, FcMatchPattern);
  FcDefaultSubstitute (pattern);
  FcResult result;
  FcPattern *match = FcFontMatch (NULL, pattern, &result);
  FcPatternDestroy (pattern);
  if (!match)
    return NULL;
  cairo_font_face_t *face = cairo_ft_font_face_create_for_pattern (match);
  FcPatternDestroy (match);
  return face;
}
