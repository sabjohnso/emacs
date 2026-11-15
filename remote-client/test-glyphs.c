/* Tests of the glyphs of the render module of the remote display client.

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

/* The pixels of a glyph come from the rasterizer of Cairo.  The
   model of the property is a reference draw: it calls Cairo for the
   glyphs of a message, with the same clip, and with the position rule
   of ftcrfont_draw.  The laws:

   - reference: after each message, the pixels of the renderer are the
     pixels of the reference.  Cairo can give other pixels for another
     set of glyphs in one draw, so the reference gives Cairo all glyphs
     of a message, as the renderer does.  The reference keeps the font of each
     font ID from the last define-font for it.  So this law also
     gives these laws:
     - clip: draw-glyphs changes no pixel outside its clip.
     - no font: draw-glyphs for a font that is not defined, or that the
       font source did not find, changes no pixel.
     - own font: a draw uses the last font of its ID, and no other
       font.
     - advance: each glyph after the first starts at the advance of
       the glyph before it, rounded to a pixel.
   - renaming: the pixels do not depend on the font ID.  Two renderers
     that get the same messages with other font IDs give the same
     pixels.

   The examples do not use the reference:
   - ink: a glyph with ink inside the clip changes a pixel.
   - bad glyph: a glyph code that the font does not have does not
     stop the draw of the glyphs after it in the same message.
   - advance: a draw of the glyphs A and B at X gives the same pixels
     as a draw of A at X, then a draw of B at X plus the advance of A,
     rounded to a pixel.
   - own font: a draw uses the font of its ID, and no other font.
   - not found: after a define-font that the font source does not
     find, a draw with that font ID changes no pixel.
   - source: the font source gets the hash, the index, and the file
     size of the define-font message.
   - large size: a define-font with a size above RC_MAX_FONT_SIZE
     defines no font, and does not stop the draws of other fonts.
     rc_renderer_has_font tells which fonts have a face: not that font,
     the other font, and not a font with no definition.

   The font source of the trials finds the fallback face, the
   fontconfig match of "monospace", for a font with a hash, and no
   face for a font with no hash.  With no argument, the program runs
   the examples, and then TRIALS trials from a seed that it gets from
   the clock and the process ID.  With the argument SEED, it runs only
   the trial of that seed.  */

#include <config.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cairo.h>

#include "fonts.h"
#include "rdproto.h"
#include "render.h"

enum { TRIALS = 200, STEPS = 20 };

/* The frame of each test.  No message defines OTHER_FRAME.  */
enum { FRAME = 1, OTHER_FRAME = 2, WIDTH = 60, HEIGHT = 40 };

/* The large font size of the trials.  */
enum { LARGE_SIZE = 1000 };

/* The font IDs of the trials.  The renaming law maps each ID to
   RENAMED_ID (ID).  */
enum { NFONTS = 3 };
#define RENAMED_ID(id) ((id) ^ 0x80000000u)

/* The fallback font source of the test.  */
static struct rc_font_source fallback;

/* A key with no hash, for the fallback source.  */
static const struct rc_font_key no_key = { NULL, 0, 0, 0 };

/* The font source of the trials.  */
static cairo_font_face_t *find_with_hash (void *,
					  const struct rc_font_key *);
static const struct rc_font_source hash_source = { find_with_hash, NULL,
						   NULL };

/* The reference of a trial: its own surface, and the scaled font of
   each font ID, or NULL.  */
struct reference
{
  cairo_surface_t *surface;
  cairo_scaled_font_t *fonts[NFONTS + 1];
};

struct pixels
{
  uint32_t values[WIDTH * HEIGHT];
};

static bool run_examples (void);
static bool ink_example (uint32_t);
static bool advance_example (void);
static bool own_font_example (void);
static bool not_found_example (void);
static bool source_example (void);
static bool large_size_example (void);
static cairo_font_face_t *record_find (void *,
					  const struct rc_font_key *);
static bool draw_changes (struct rc_renderer *, uint32_t);
static bool run_trial (uint64_t);
static void reference_apply (struct reference *,
			     const struct rd_message *);
static void reference_draw (struct reference *,
			    const struct rd_draw_glyphs *);
static cairo_scaled_font_t *reference_font (uint32_t);
static void reference_free (struct reference *);
static void surface_pixels (cairo_surface_t *, struct pixels *);
static struct rc_renderer *new_renderer (void);
static void apply_both (struct rc_renderer *, struct rc_renderer *,
			const struct rd_message *);
static struct rd_message renamed (const struct rd_message *);
static void get_pixels (struct rc_renderer *, struct pixels *);
static bool same_pixels (const struct pixels *, const struct pixels *);
static int glyph_index (const char *);
static int glyph_advance (int, int);
static struct rd_message define_frame (void);
static struct rd_message define_font (uint32_t, uint32_t);
static struct rd_message clear_frame (uint32_t);
static struct rd_message draw_glyphs (uint32_t, int32_t, int32_t,
				      const uint32_t *, int);
static struct rd_message random_message (uint64_t *);
static uint32_t random_font_size (uint64_t *);
static int32_t random_position (uint64_t *);
static int32_t random_pen (uint64_t *, int);
static uint32_t random_size (uint64_t *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

int
main (int argc, char **argv)
{
  fallback = rc_fallback_font_source_new ();
  if (!fallback.data)
    {
      fprintf (stderr, "test-glyphs: fontconfig has no match for "
	       "\"monospace\"\n");
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
	printf ("test-glyphs: the examples and %d trials passed\n", TRIALS);
    }
  rc_font_source_free (fallback);
  return passed ? 0 : 1;
}

/* The examples.  */

static bool
run_examples (void)
{
  bool ink = ink_example (0);
  bool bad_glyph = ink_example (UINT32_MAX - 1);
  bool advance = advance_example ();
  bool own_font = own_font_example ();
  bool not_found = not_found_example ();
  bool source = source_example ();
  bool large_size = large_size_example ();
  return (ink && bad_glyph && advance && own_font && not_found && source
	  && large_size);
}

/* The ink law: a glyph "M" inside the clip changes a pixel.  If BAD
   is not 0, the glyph code BAD comes before the glyph "M" in the same
   message, for the bad glyph law.  */
static bool
ink_example (uint32_t bad)
{
  struct rc_renderer *renderer = new_renderer ();
  struct rd_message messages[] =
    {
      define_frame (), define_font (7, 20), clear_frame (0xffffff),
      (bad ? draw_glyphs (7, 5, 25,
			  (uint32_t[]) { bad, glyph_index ("M") }, 2)
       : draw_glyphs (7, 5, 25, (uint32_t[]) { glyph_index ("M") }, 1)),
    };
  struct pixels before;
  for (int i = 0; i < 3; i++)
    rc_apply (renderer, &messages[i]);
  get_pixels (renderer, &before);
  rc_apply (renderer, &messages[3]);
  struct pixels after;
  get_pixels (renderer, &after);
  rc_renderer_free (renderer);
  if (same_pixels (&before, &after))
    {
      fprintf (stderr, "test-glyphs: example %s: the glyph changed no "
	       "pixel\n", bad ? "bad glyph" : "ink");
      return false;
    }
  return true;
}

/* The advance law for the glyphs "l" and "i", which have no common
   pixels.  */
static bool
advance_example (void)
{
  int l = glyph_index ("l"), i = glyph_index ("i");
  struct rc_renderer *together = new_renderer ();
  struct rc_renderer *apart = new_renderer ();
  struct rd_message start[] = { define_frame (), define_font (7, 20),
				clear_frame (0xffffff) };
  for (int j = 0; j < 3; j++)
    {
      rc_apply (together, &start[j]);
      rc_apply (apart, &start[j]);
    }
  struct rd_message both = draw_glyphs (7, 5, 25,
					(uint32_t[]) { l, i }, 2);
  struct rd_message first = draw_glyphs (7, 5, 25, (uint32_t[]) { l }, 1);
  struct rd_message second
    = draw_glyphs (7, 5 + glyph_advance (l, 20), 25,
		   (uint32_t[]) { i }, 1);
  rc_apply (together, &both);
  rc_apply (apart, &first);
  rc_apply (apart, &second);
  struct pixels a, b;
  get_pixels (together, &a);
  get_pixels (apart, &b);
  rc_renderer_free (together);
  rc_renderer_free (apart);
  if (!same_pixels (&a, &b))
    {
      fprintf (stderr, "test-glyphs: example advance: the pixels of two "
	       "draws are not the pixels of one draw\n");
      return false;
    }
  return true;
}

/* The own font law: with the fonts 1 at the size 10 and 2 at the
   size 30, a draw with the font 2 gives the pixels of a renderer that
   has only the font 2.  */
static bool
own_font_example (void)
{
  struct rc_renderer *both = new_renderer ();
  struct rc_renderer *one = new_renderer ();
  struct rd_message messages[] =
    {
      define_frame (), clear_frame (0xffffff), define_font (1, 10),
      define_font (2, 30),
      draw_glyphs (2, 5, 30, (uint32_t[]) { glyph_index ("M") }, 1),
    };
  for (int i = 0; i < 5; i++)
    {
      rc_apply (both, &messages[i]);
      if (i != 2)
	rc_apply (one, &messages[i]);
    }
  struct pixels a, b;
  get_pixels (both, &a);
  get_pixels (one, &b);
  rc_renderer_free (both);
  rc_renderer_free (one);
  if (!same_pixels (&a, &b))
    {
      fprintf (stderr, "test-glyphs: example own font: the draw did not "
	       "use the font of its ID\n");
      return false;
    }
  return true;
}

/* The not found law, with a font source that finds a face only for a
   font with a hash.  The font 7 first has a hash, and then has
   none.  */
static bool
not_found_example (void)
{
  struct rc_renderer *renderer
    = rc_renderer_new ((struct rc_font_source) { find_with_hash, NULL,
						 NULL });
  struct rd_message messages[] = { define_frame (), define_font (7, 20) };
  messages[1].u.define_font.nhash = 1;
  rc_apply (renderer, &messages[0]);
  rc_apply (renderer, &messages[1]);
  bool found = draw_changes (renderer, 7);
  messages[1].u.define_font.nhash = 0;
  rc_apply (renderer, &messages[1]);
  bool not_found = !draw_changes (renderer, 7);
  rc_renderer_free (renderer);
  if (!found || !not_found)
    {
      fprintf (stderr, "test-glyphs: example not found: a font that the "
	       "source did not find drew a glyph\n");
      return false;
    }
  return true;
}

/* The source law, with a font source that records the arguments of
   its FIND function.  */
static bool
source_example (void)
{
  uint8_t recorded[5] = { 0 };
  struct rc_renderer *renderer
    = rc_renderer_new ((struct rc_font_source) { record_find, NULL,
						 recorded });
  struct rd_message define = define_font (7, 20);
  define.u.define_font.index = 3;
  define.u.define_font.nhash = 2;
  define.u.define_font.hash[0] = 0xab;
  define.u.define_font.hash[1] = 0xcd;
  define.u.define_font.file_size = 0x155;
  rc_apply (renderer, &define);
  rc_renderer_free (renderer);
  if (recorded[0] != 0xab || recorded[1] != 0xcd || recorded[2] != 2
      || recorded[3] != 3 || recorded[4] != 0x55)
    {
      fprintf (stderr, "test-glyphs: example source: the source did not "
	       "get the hash, the index, and the file size\n");
      return false;
    }
  return true;
}

/* The large size law: after a define-font of the font 8 with the size
   UINT32_MAX, the font 8 draws nothing, and the font 7 draws.  */
static bool
large_size_example (void)
{
  struct rc_renderer *renderer = new_renderer ();
  struct rd_message messages[] = { define_frame (),
				   define_font (8, UINT32_MAX),
				   define_font (7, 20) };
  for (int i = 0; i < 3; i++)
    rc_apply (renderer, &messages[i]);
  bool large = draw_changes (renderer, 8);
  bool other = draw_changes (renderer, 7);
  bool faces = (!rc_renderer_has_font (renderer, 8)
		&& rc_renderer_has_font (renderer, 7)
		&& !rc_renderer_has_font (renderer, 9));
  rc_renderer_free (renderer);
  if (large || !other || !faces)
    {
      fprintf (stderr, "test-glyphs: example large size: the font 8 "
	       "drew, the font 7 did not draw, or a face is not "
	       "correct\n");
      return false;
    }
  return true;
}

/* The FIND function of a font source that records the first two bytes
   of the hash of KEY, its number of bytes, its index, and the low byte
   of its file size in DATA, an array of 5 bytes.  It finds no face.  */
static cairo_font_face_t *
record_find (void *data, const struct rc_font_key *key)
{
  uint8_t *recorded = data;
  recorded[0] = key->nhash > 0 ? key->hash[0] : 0;
  recorded[1] = key->nhash > 1 ? key->hash[1] : 0;
  recorded[2] = key->nhash;
  recorded[3] = key->index;
  recorded[4] = key->file_size;
  return NULL;
}

/* The FIND function of a font source that finds the fallback face for
   a font with a hash, and no face for a font with no hash.  */
static cairo_font_face_t *
find_with_hash (void *data, const struct rc_font_key *key)
{
  (void) data;
  return key->nhash ? fallback.find (fallback.data, key) : NULL;
}

/* Clear the frame of RENDERER, draw the glyph "M" with FONT, and
   return true if the draw changed a pixel.  */
static bool
draw_changes (struct rc_renderer *renderer, uint32_t font)
{
  struct rd_message clear = clear_frame (0xffffff);
  struct rd_message draw
    = draw_glyphs (font, 5, 25, (uint32_t[]) { glyph_index ("M") }, 1);
  struct pixels before, after;
  rc_apply (renderer, &clear);
  get_pixels (renderer, &before);
  rc_apply (renderer, &draw);
  get_pixels (renderer, &after);
  return !same_pixels (&before, &after);
}

/* The property.  */

/* Run the trial of SEED: a definition of each font, and then a random
   sequence of messages for one frame.  Check the reference law after
   each message, and the renaming law at the end.  */
static bool
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  struct rc_renderer *renderer = rc_renderer_new (hash_source);
  struct rc_renderer *renamed_renderer = rc_renderer_new (hash_source);
  struct reference reference =
    { .surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, WIDTH,
					    HEIGHT) };
  struct rd_message start = define_frame ();
  apply_both (renderer, renamed_renderer, &start);
  for (int font = 0; font < NFONTS; font++)
    {
      struct rd_message define = define_font (font,
					      1 + random_below (&state, 40));
      define.u.define_font.nhash = 1;
      apply_both (renderer, renamed_renderer, &define);
      reference_apply (&reference, &define);
    }
  bool passed = true;
  for (int step = 0; passed && step < STEPS; step++)
    {
      struct rd_message message = random_message (&state);
      apply_both (renderer, renamed_renderer, &message);
      reference_apply (&reference, &message);
      struct pixels actual, expected;
      get_pixels (renderer, &actual);
      surface_pixels (reference.surface, &expected);
      passed = same_pixels (&actual, &expected);
      if (!passed)
	fprintf (stderr, "test-glyphs: seed %llu, step %d: the pixels are "
		 "not the pixels of the reference\n",
		 (unsigned long long) seed, step);
    }
  struct pixels a, b;
  get_pixels (renderer, &a);
  get_pixels (renamed_renderer, &b);
  if (passed && !same_pixels (&a, &b))
    {
      fprintf (stderr, "test-glyphs: seed %llu: the pixels depend on "
	       "the font ID\n", (unsigned long long) seed);
      passed = false;
    }
  rc_renderer_free (renderer);
  rc_renderer_free (renamed_renderer);
  reference_free (&reference);
  return passed;
}

/* The reference.  */

/* Apply MESSAGE to REFERENCE.  The messages of a trial are define-font,
   a clear of the whole frame, and draw-glyphs.  */
static void
reference_apply (struct reference *reference,
		 const struct rd_message *message)
{
  const struct rd_define_font *font = &message->u.define_font;
  const struct rd_clear_area *clear = &message->u.clear_area;
  cairo_t *cr;
  switch (message->type)
    {
    case RD_DEFINE_FONT:
      if (reference->fonts[font->font])
	cairo_scaled_font_destroy (reference->fonts[font->font]);
      reference->fonts[font->font]
	= font->nhash ? reference_font (font->size) : NULL;
      break;
    case RD_CLEAR_AREA:
      cr = cairo_create (reference->surface);
      cairo_set_source_rgb (cr, ((clear->color >> 16) & 0xff) / 255.0,
			    ((clear->color >> 8) & 0xff) / 255.0,
			    (clear->color & 0xff) / 255.0);
      cairo_paint (cr);
      cairo_destroy (cr);
      break;
    case RD_DRAW_GLYPHS:
      reference_draw (reference, &message->u.draw_glyphs);
      break;
    default:
      break;
    }
}

/* Draw GLYPHS in REFERENCE, in the clip of GLYPHS.  */
static void
reference_draw (struct reference *reference,
		const struct rd_draw_glyphs *glyphs)
{
  cairo_scaled_font_t *font = reference->fonts[glyphs->font];
  int64_t left = glyphs->clip_x > 0 ? glyphs->clip_x : 0;
  int64_t top = glyphs->clip_y > 0 ? glyphs->clip_y : 0;
  int64_t right = (int64_t) glyphs->clip_x + glyphs->clip_width;
  int64_t bottom = (int64_t) glyphs->clip_y + glyphs->clip_height;
  right = right < WIDTH ? right : WIDTH;
  bottom = bottom < HEIGHT ? bottom : HEIGHT;
  if (glyphs->frame != FRAME || !font || right <= left || bottom <= top)
    return;
  cairo_glyph_t positions[RD_MAX_GLYPHS];
  int npositions = 0;
  double x = glyphs->x;
  for (int i = 0; i < glyphs->nglyphs; i++)
    {
      cairo_glyph_t glyph = { glyphs->glyphs[i], 0, 0 };
      cairo_text_extents_t extents;
      cairo_scaled_font_glyph_extents (font, &glyph, 1, &extents);
      positions[npositions++]
	= (cairo_glyph_t) { glyph.index, x, glyphs->y };
      x += lround (extents.x_advance);
    }
  cairo_t *cr = cairo_create (reference->surface);
  cairo_rectangle (cr, left, top, right - left, bottom - top);
  cairo_clip (cr);
  cairo_set_source_rgb (cr, ((glyphs->color >> 16) & 0xff) / 255.0,
			((glyphs->color >> 8) & 0xff) / 255.0,
			(glyphs->color & 0xff) / 255.0);
  cairo_set_scaled_font (cr, font);
  cairo_show_glyphs (cr, positions, npositions);
  cairo_destroy (cr);
}

/* Return a new scaled font of the fallback face at SIZE, or NULL if
   SIZE is above RC_MAX_FONT_SIZE or Cairo cannot make it.  */
static cairo_scaled_font_t *
reference_font (uint32_t size)
{
  if (size > RC_MAX_FONT_SIZE)
    return NULL;
  cairo_font_face_t *face = fallback.find (fallback.data, &no_key);
  cairo_matrix_t matrix, identity;
  cairo_matrix_init_scale (&matrix, size, size);
  cairo_matrix_init_identity (&identity);
  cairo_font_options_t *options = cairo_font_options_create ();
  cairo_scaled_font_t *font = cairo_scaled_font_create (face, &matrix,
							&identity, options);
  cairo_font_options_destroy (options);
  cairo_font_face_destroy (face);
  if (cairo_scaled_font_status (font) == CAIRO_STATUS_SUCCESS)
    return font;
  cairo_scaled_font_destroy (font);
  return NULL;
}

static void
reference_free (struct reference *reference)
{
  cairo_surface_destroy (reference->surface);
  for (int i = 0; i <= NFONTS; i++)
    if (reference->fonts[i])
      cairo_scaled_font_destroy (reference->fonts[i]);
}

/* Return a new renderer with the fallback font source.  */
static struct rc_renderer *
new_renderer (void)
{
  return rc_renderer_new (fallback);
}

/* Apply MESSAGE to RENDERER, and the renamed MESSAGE to RENAMED.  */
static void
apply_both (struct rc_renderer *renderer, struct rc_renderer *renamed_to,
	    const struct rd_message *message)
{
  struct rd_message other = renamed (message);
  rc_apply (renderer, message);
  rc_apply (renamed_to, &other);
}

/* Return MESSAGE with each font ID renamed.  */
static struct rd_message
renamed (const struct rd_message *message)
{
  struct rd_message other = *message;
  if (other.type == RD_DEFINE_FONT)
    other.u.define_font.font = RENAMED_ID (other.u.define_font.font);
  else if (other.type == RD_DRAW_GLYPHS)
    other.u.draw_glyphs.font = RENAMED_ID (other.u.draw_glyphs.font);
  return other;
}

/* Copy the pixels of the frame of RENDERER to PIXELS.  */
static void
get_pixels (struct rc_renderer *renderer, struct pixels *pixels)
{
  surface_pixels (rc_frame_surface (renderer, FRAME), pixels);
}

/* Copy the pixels of SURFACE, of the size of the frame, to PIXELS.  */
static void
surface_pixels (cairo_surface_t *surface, struct pixels *pixels)
{
  cairo_surface_flush (surface);
  const unsigned char *data = cairo_image_surface_get_data (surface);
  int stride = cairo_image_surface_get_stride (surface);
  for (int y = 0; y < HEIGHT; y++)
    for (int x = 0; x < WIDTH; x++)
      {
	uint32_t pixel;
	memcpy (&pixel, data + y * stride + x * 4, sizeof pixel);
	pixels->values[y * WIDTH + x] = pixel & 0xffffff;
      }
}

static bool
same_pixels (const struct pixels *a, const struct pixels *b)
{
  return memcmp (a->values, b->values, sizeof a->values) == 0;
}

/* Return the glyph index of the first character of TEXT in the
   fallback font.  */
static int
glyph_index (const char *text)
{
  cairo_font_face_t *face = fallback.find (fallback.data, &no_key);
  cairo_matrix_t size, identity;
  cairo_matrix_init_scale (&size, 20, 20);
  cairo_matrix_init_identity (&identity);
  cairo_font_options_t *options = cairo_font_options_create ();
  cairo_scaled_font_t *font = cairo_scaled_font_create (face, &size,
							&identity,
							options);
  cairo_glyph_t *glyphs = NULL;
  int nglyphs = 0;
  cairo_scaled_font_text_to_glyphs (font, 0, 0, text, 1, &glyphs,
				    &nglyphs, NULL, NULL, NULL);
  int index = nglyphs ? glyphs[0].index : 0;
  cairo_glyph_free (glyphs);
  cairo_scaled_font_destroy (font);
  cairo_font_options_destroy (options);
  cairo_font_face_destroy (face);
  return index;
}

/* Return the advance of the glyph INDEX of the fallback font at the
   size SIZE, rounded to a pixel.  */
static int
glyph_advance (int index, int size)
{
  cairo_font_face_t *face = fallback.find (fallback.data, &no_key);
  cairo_matrix_t matrix, identity;
  cairo_matrix_init_scale (&matrix, size, size);
  cairo_matrix_init_identity (&identity);
  cairo_font_options_t *options = cairo_font_options_create ();
  cairo_scaled_font_t *font = cairo_scaled_font_create (face, &matrix,
							&identity,
							options);
  cairo_glyph_t glyph = { index, 0, 0 };
  cairo_text_extents_t extents;
  cairo_scaled_font_glyph_extents (font, &glyph, 1, &extents);
  cairo_scaled_font_destroy (font);
  cairo_font_options_destroy (options);
  cairo_font_face_destroy (face);
  return lround (extents.x_advance);
}

/* The messages.  */

static struct rd_message
define_frame (void)
{
  return (struct rd_message) { .type = RD_DEFINE_FRAME,
			       .u.define_frame = { FRAME, WIDTH, HEIGHT } };
}

/* Return a define-font message for the ID FONT at the size SIZE.  */
static struct rd_message
define_font (uint32_t font, uint32_t size)
{
  return (struct rd_message) { .type = RD_DEFINE_FONT,
			       .u.define_font = { .font = font,
						  .size = size } };
}

/* Return a clear-area message that fills the frame with COLOR.  */
static struct rd_message
clear_frame (uint32_t color)
{
  return (struct rd_message)
    { .type = RD_CLEAR_AREA,
      .u.clear_area = { FRAME, 0, 0, WIDTH, HEIGHT, color } };
}

/* Return a draw-glyphs message for the N GLYPHS of FONT at X, Y in
   black, with a clip that is the frame.  */
static struct rd_message
draw_glyphs (uint32_t font, int32_t x, int32_t y, const uint32_t *glyphs,
	     int n)
{
  struct rd_message message =
    { .type = RD_DRAW_GLYPHS,
      .u.draw_glyphs = { .frame = FRAME, .x = x, .y = y, .clip_x = 0,
			 .clip_y = 0, .clip_width = WIDTH,
			 .clip_height = HEIGHT, .color = 0, .font = font,
			 .nglyphs = n } };
  memcpy (message.u.draw_glyphs.glyphs, glyphs, n * sizeof *glyphs);
  return message;
}

/* The generators.  */

/* Return a random message: a define-font, a clear-area of the frame,
   or a draw-glyphs.  A font ID is from 0 to NFONTS, and the font ID
   NFONTS is never defined.  A quarter of the define-font messages have
   no hash, so the font source of the trials does not find them.  */
static struct rd_message
random_message (uint64_t *state)
{
  switch (random_below (state, 6))
    {
    case 0:
      {
	struct rd_message message
	  = define_font (random_below (state, NFONTS),
			 random_font_size (state));
	message.u.define_font.nhash = random_below (state, 4) != 0;
	message.u.define_font.hash[0] = random_next (state);
	return message;
      }
    case 1:
      return clear_frame (random_next (state));
    default:
      {
	struct rd_message message = { .type = RD_DRAW_GLYPHS };
	struct rd_draw_glyphs *m = &message.u.draw_glyphs;
	m->frame = random_below (state, 16) ? FRAME : OTHER_FRAME;
	m->x = random_pen (state, WIDTH);
	m->y = random_pen (state, HEIGHT);
	bool whole = random_below (state, 2);
	m->clip_x = whole ? 0 : random_position (state);
	m->clip_y = whole ? 0 : random_position (state);
	m->clip_width = whole ? WIDTH : random_size (state);
	m->clip_height = whole ? HEIGHT : random_size (state);
	m->color = random_next (state);
	m->font = random_below (state, NFONTS + 1);
	m->nglyphs = (random_below (state, 16) == 0
		      ? random_below (state, RD_MAX_GLYPHS + 1)
		      : random_below (state, 6));
	for (int i = 0; i < m->nglyphs; i++)
	  m->glyphs[i] = (random_below (state, 16) == 0 ? random_next (state)
			  : random_below (state, 300));
	return message;
      }
    }
}

/* Return a random font size.  Most sizes are from 1 to 40.  Some are
   0, LARGE_SIZE, or the maximum of the field.  */
static uint32_t
random_font_size (uint64_t *state)
{
  switch (random_below (state, 16))
    {
    case 0:
      return 0;
    case 1:
      return LARGE_SIZE;
    case 2:
      return UINT32_MAX;
    default:
      return 1 + random_below (state, 40);
    }
}

/* Return a random position of a pen on an axis of the frame with
   LENGTH pixels.  Most positions are in the frame, so most glyphs
   change pixels, and some are at the limits of the field.  */
static int32_t
random_pen (uint64_t *state, int length)
{
  if (random_below (state, 8) == 0)
    return random_position (state);
  return (int32_t) random_below (state, length + 10) - 5;
}

/* Return a random position.  Most positions are near the frame, and
   some are at the limits of the field.  */
static int32_t
random_position (uint64_t *state)
{
  switch (random_below (state, 32))
    {
    case 0:
      return INT32_MIN;
    case 1:
      return INT32_MAX;
    default:
      return (int32_t) random_below (state, WIDTH + 41) - 20;
    }
}

/* Return a random size of a clip.  Some sizes are the maximum of the
   field.  */
static uint32_t
random_size (uint64_t *state)
{
  if (random_below (state, 32) == 0)
    return UINT32_MAX;
  return random_below (state, WIDTH + 1);
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
