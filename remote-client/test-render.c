/* Tests of the render module of the remote display client.

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

/* The main test is a property with a model.  A trial applies a random
   sequence of messages to a renderer and to the model.  The model
   keeps a plain array of pixels for each frame, and it applies each
   message one pixel at a time.  After each message, the frames of the
   renderer must have the format CAIRO_FORMAT_RGB24, and the same
   sizes and the same pixels as the frames of the model.  The model
   gives these laws:

   - clear-area sets its rectangle, and only its rectangle, to its
     color.
   - copy-area gives the same pixels as a move of the source rectangle.
     A pixel of the destination whose source is outside the frame does
     not change.
   - draw-cursor fills its rectangle for a box, a bar, and an hbar.  A
     hollow box sets only the pixels at the edges of the rectangle.  A
     kind that the client does not know changes no pixel.
   - draw-fringe sets each bit that is 1 to the foreground, and each
     bit that is 0 to the background.  An overlay does not change the
     pixels of the bits that are 0.
   - define-frame makes a frame with all pixels 0.  For a frame that
     exists, it changes the size and keeps the pixels that are in both
     sizes.  A size above RC_MAX_FRAME_SIZE deletes the frame.
   - delete-frame removes a frame.
   - hello and define-font change no frame.
   - A message for a frame that does not exist changes no pixel.
   - Each color ignores its high 8 bits.

   One more law does not use the model: a second apply of a message
   that is not copy-area changes nothing.

   The model and the renderer can have the same wrong idea of a law.
   The examples give pixels that a person calculated, so they do not
   use the model.

   With no argument, the program runs the examples, and then TRIALS
   trials from a seed that it gets from the clock and the process ID.
   With the argument SEED, it runs only the trial of that seed.  On a
   failure, it writes the seed of the trial and the step, and exits
   with the status 1.  */

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cairo.h>

#include "rdproto.h"
#include "render.h"

enum { TRIALS = 300, STEPS = 40 };

/* The frame IDs of a trial.  The set is small, so many messages are
   for a frame that exists, and some are for a frame that does not.
   It has more frames than the first size of the frame table of the
   renderer.  */
static const uint32_t frame_ids[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8,
				      UINT32_MAX };
enum { NFRAMES = sizeof frame_ids / sizeof *frame_ids };

/* A typical frame of a trial has at most this size.  */
enum { TYPICAL_SIZE = 40 };

struct model_frame
{
  bool defined;
  int width, height;
  uint32_t *pixels;
};

/* The frames of the model, in the order of frame_ids.  */
struct model
{
  struct model_frame frames[NFRAMES];
};

/* A rectangle with 64-bit fields, so the sum of a position and a size
   of the protocol does not overflow.  */
struct rect
{
  int64_t x, y, width, height;
};

/* A test with pixels that a person calculated.  After the messages,
   the frame 1 must have the size WIDTH, HEIGHT and the pixels
   PIXELS, row by row.  */
struct example
{
  const char *name;
  const struct rd_message *messages;
  int nmessages;
  int width, height;
  const uint32_t *pixels;
};

static bool run_examples (void);
static bool run_example (const struct example *);
static bool run_trial (uint64_t);
static bool idempotent_p (const struct rd_message *);
static bool check_frames (const struct model *, struct rc_renderer *,
			  uint64_t, int, const struct rd_message *);
static void write_message (const struct rd_message *);
static const char *frame_problem (const struct model_frame *,
				  cairo_surface_t *, char *, size_t);
static const char *pixel_problem (const uint32_t *, cairo_surface_t *,
				  char *, size_t);

static void model_apply (struct model *, const struct rd_message *);
static void model_define (struct model *, uint32_t, uint32_t, uint32_t);
static void model_delete (struct model *, uint32_t);
static void model_clear (struct model_frame *, struct rect, uint32_t);
static void model_copy (struct model_frame *, const struct rd_copy_area *);
static void model_cursor (struct model_frame *,
			  const struct rd_draw_cursor *);
static void model_hollow_box (struct model_frame *, struct rect,
			      uint32_t);
static void model_fringe (struct model_frame *,
			  const struct rd_draw_fringe *);
static int fringe_bit (uint16_t, int, int);
static struct model_frame *model_frame (struct model *, uint32_t);
static bool inside_rect (struct rect, int64_t, int64_t);
static bool inside_frame (const struct model_frame *, int64_t, int64_t);
static void model_set (struct model_frame *, int64_t, int64_t, uint32_t);
static void model_free (struct model *);

static struct rd_message random_message (uint64_t *);
static struct rd_message random_define_frame (uint64_t *, uint32_t);
static struct rd_message random_copy_area (uint64_t *, uint32_t);
static struct rd_message random_draw_fringe (uint64_t *, uint32_t,
					     uint32_t);
static struct rd_message random_define_font (uint64_t *);
static uint32_t random_frame_size (uint64_t *);
static uint8_t random_flag (uint64_t *);
static int32_t random_position (uint64_t *);
static uint32_t random_size (uint64_t *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 0 : 1;
  if (!run_examples ())
    return 1;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    if (!run_trial (base + trial))
      return 1;
  printf ("test-render: the examples and %d trials passed\n", TRIALS);
  return 0;
}

/* The examples.  */

#define DEFINE(W, H)							\
  { RD_DEFINE_FRAME, .u.define_frame = { 1, W, H } }
#define CLEAR(X, Y, W, H, COLOR)					\
  { RD_CLEAR_AREA, .u.clear_area = { 1, X, Y, W, H, COLOR } }
#define COPY(X, Y, W, H, TO_X, TO_Y)					\
  { RD_COPY_AREA, .u.copy_area = { 1, X, Y, W, H, TO_X, TO_Y } }
#define HOLLOW(X, Y, W, H, COLOR)					\
  { RD_DRAW_CURSOR,							\
    .u.draw_cursor = { 1, X, Y, W, H, RD_CURSOR_HOLLOW_BOX, COLOR } }
#define FRINGE(WIDTH, ROW)						\
  { RD_DRAW_FRINGE,							\
    .u.draw_fringe = { 1, 0, 0, WIDTH, 0, 0, 0xff0000, 0x0000ff, 1,	\
		       { ROW } } }
#define EXAMPLE(NAME, MESSAGES, W, H, PIXELS)				\
  { NAME, MESSAGES, sizeof MESSAGES / sizeof *MESSAGES, W, H, PIXELS }

/* A copy of the top left 2 x 2 pixels one pixel to the right and one
   pixel down.  The source and the destination overlap.  */
static const struct rd_message overlap_messages[] =
  {
    DEFINE (3, 3),
    CLEAR (0, 0, 1, 1, 1), CLEAR (1, 0, 1, 1, 2), CLEAR (2, 0, 1, 1, 3),
    CLEAR (0, 1, 1, 1, 4), CLEAR (1, 1, 1, 1, 5), CLEAR (2, 1, 1, 1, 6),
    CLEAR (0, 2, 1, 1, 7), CLEAR (1, 2, 1, 1, 8), CLEAR (2, 2, 1, 1, 9),
    COPY (0, 0, 2, 2, 1, 1),
  };
static const uint32_t overlap_pixels[] = { 1, 2, 3,
					   4, 1, 2,
					   7, 4, 5 };

/* A copy to the left from a source that is not at the origin.  */
static const struct rd_message copy_left_messages[] =
  {
    DEFINE (3, 1),
    CLEAR (0, 0, 1, 1, 1), CLEAR (1, 0, 1, 1, 2), CLEAR (2, 0, 1, 1, 3),
    COPY (1, 0, 2, 1, 0, 0),
  };
static const uint32_t copy_left_pixels[] = { 2, 3, 3 };

/* A hollow box of 3 x 3 pixels, and a hollow box one pixel wide.  */
static const struct rd_message hollow_messages[] =
  {
    DEFINE (4, 3), HOLLOW (0, 0, 3, 3, 7), HOLLOW (3, 0, 1, 3, 8),
  };
static const uint32_t hollow_pixels[] = { 7, 7, 7, 8,
					  7, 0, 7, 8,
					  7, 7, 7, 8 };

/* Bit 7 of a fringe 8 pixels wide is its leftmost pixel.  */
static const struct rd_message fringe_left_messages[] =
  {
    DEFINE (8, 1), FRINGE (8, 0x80),
  };
static const uint32_t fringe_left_pixels[] =
  { 0xff0000, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* A fringe 18 pixels wide: the 2 pixels to the left of bit 15 have
   the bit 0.  */
static const struct rd_message fringe_wide_messages[] =
  {
    DEFINE (18, 1), FRINGE (18, 0xffff),
  };
static const uint32_t fringe_wide_pixels[] =
  { 0xff, 0xff, 0xff0000, 0xff0000, 0xff0000, 0xff0000, 0xff0000,
    0xff0000, 0xff0000, 0xff0000, 0xff0000, 0xff0000, 0xff0000,
    0xff0000, 0xff0000, 0xff0000, 0xff0000, 0xff0000 };

/* A color with high bits that are not 0.  */
static const struct rd_message high_bits_messages[] =
  {
    DEFINE (1, 1), CLEAR (0, 0, 1, 1, 0xff123456),
  };
static const uint32_t high_bits_pixels[] = { 0x123456 };

/* A frame that becomes larger, then smaller: the new pixels are 0.  */
static const struct rd_message resize_messages[] =
  {
    DEFINE (1, 1), CLEAR (0, 0, 1, 1, 5), DEFINE (2, 1),
    CLEAR (0, 0, 2, 1, 6), DEFINE (1, 1), DEFINE (2, 2),
  };
static const uint32_t resize_pixels[] = { 6, 0,
					  0, 0 };

/* A frame that becomes as wide as RC_MAX_FRAME_SIZE, then small
   again, keeps its pixels.  */
static const struct rd_message widest_messages[] =
  {
    DEFINE (8, 1), CLEAR (0, 0, 8, 1, 9), DEFINE (RC_MAX_FRAME_SIZE, 1),
    DEFINE (8, 1),
  };
static const uint32_t widest_pixels[] = { 9, 9, 9, 9, 9, 9, 9, 9 };

static const struct example examples[] =
  {
    EXAMPLE ("overlap copy", overlap_messages, 3, 3, overlap_pixels),
    EXAMPLE ("copy left", copy_left_messages, 3, 1, copy_left_pixels),
    EXAMPLE ("hollow box", hollow_messages, 4, 3, hollow_pixels),
    EXAMPLE ("fringe left bit", fringe_left_messages, 8, 1,
	     fringe_left_pixels),
    EXAMPLE ("wide fringe", fringe_wide_messages, 18, 1,
	     fringe_wide_pixels),
    EXAMPLE ("high color bits", high_bits_messages, 1, 1,
	     high_bits_pixels),
    EXAMPLE ("resize", resize_messages, 2, 2, resize_pixels),
    EXAMPLE ("widest frame", widest_messages, 8, 1, widest_pixels),
  };

/* Return true if each example passes.  */
static bool
run_examples (void)
{
  bool passed = true;
  for (size_t i = 0; i < sizeof examples / sizeof *examples; i++)
    passed &= run_example (&examples[i]);
  return passed;
}

/* Return true if EXAMPLE passes.  Else write the problem.  */
static bool
run_example (const struct example *example)
{
  struct rc_renderer *renderer = rc_renderer_new ();
  for (int i = 0; i < example->nmessages; i++)
    rc_apply (renderer, &example->messages[i]);
  struct model_frame expected = { true, example->width, example->height,
				  (uint32_t *) example->pixels };
  char text[100];
  const char *problem = frame_problem (&expected,
				       rc_frame_surface (renderer, 1),
				       text, sizeof text);
  if (problem)
    fprintf (stderr, "test-render: example %s: %s\n", example->name,
	     problem);
  rc_renderer_free (renderer);
  return !problem;
}

/* The property.  */

/* Run the trial of SEED.  Return true if it passes.  */
static bool
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  struct model model = { 0 };
  struct rc_renderer *renderer = rc_renderer_new ();
  bool passed = true;
  for (int step = 0; passed && step < STEPS; step++)
    {
      struct rd_message message = random_message (&state);
      model_apply (&model, &message);
      rc_apply (renderer, &message);
      passed = check_frames (&model, renderer, seed, step, &message);
      if (passed && idempotent_p (&message))
	{
	  rc_apply (renderer, &message);
	  passed = check_frames (&model, renderer, seed, step, &message);
	}
    }
  rc_renderer_free (renderer);
  model_free (&model);
  return passed;
}

/* Return true if a second apply of MESSAGE changes nothing.  */
static bool
idempotent_p (const struct rd_message *message)
{
  return message->type != RD_COPY_AREA;
}

/* Return true if each frame of RENDERER equals the frame of MODEL with
   the same ID.  Else report the first difference of the trial SEED at
   STEP, after MESSAGE.  */
static bool
check_frames (const struct model *model, struct rc_renderer *renderer,
	      uint64_t seed, int step, const struct rd_message *message)
{
  for (int i = 0; i < NFRAMES; i++)
    {
      char text[100];
      const char *problem
	= frame_problem (&model->frames[i],
			 rc_frame_surface (renderer, frame_ids[i]),
			 text, sizeof text);
      if (problem)
	{
	  fprintf (stderr, "test-render: seed %llu, step %d, frame %lu: "
		   "%s\nafter ", (unsigned long long) seed, step,
		   (unsigned long) frame_ids[i], problem);
	  write_message (message);
	  return false;
	}
    }
  return true;
}

/* Write the name and the fields of MESSAGE to the standard error
   output.  For an array, write only its number of elements.  */
static void
write_message (const struct rd_message *message)
{
  const struct rd_format *format = &rd_formats[message->type];
  fprintf (stderr, "%s", format->name);
  for (int i = 0; i < format->nfields; i++)
    {
      const struct rd_field *f = &format->fields[i];
      if (f->max_count)
	fprintf (stderr, " %s: %d elements", f->name,
		 rd_array_count (message, f));
      else
	fprintf (stderr, " %s: %lld", f->name,
		 (long long) rd_field_value (message, f, 0));
    }
  fprintf (stderr, "\n");
}

/* Return NULL if SURFACE has the format, the size, and the pixels of
   FRAME.  A frame that is not defined must have no surface.  Else
   return the problem, which can be in TEXT of SIZE bytes.  */
static const char *
frame_problem (const struct model_frame *frame, cairo_surface_t *surface,
	       char *text, size_t size)
{
  if (!frame->defined || !surface)
    {
      if (frame->defined == !!surface)
	return NULL;
      return (frame->defined ? "the frame has no surface"
	      : "a frame that is not defined has a surface");
    }
  if (cairo_image_surface_get_format (surface) != CAIRO_FORMAT_RGB24)
    return "the format is not CAIRO_FORMAT_RGB24";
  if (cairo_image_surface_get_width (surface) != frame->width
      || cairo_image_surface_get_height (surface) != frame->height)
    return "the size is not correct";
  return pixel_problem (frame->pixels, surface, text, size);
}

/* Return NULL if SURFACE has the pixels EXPECTED, row by row.  Else
   write the first pixel that is not correct to TEXT of SIZE bytes, and
   return TEXT.  A pixel of CAIRO_FORMAT_RGB24 is a native uint32_t,
   and its high 8 bits are not part of the color.  */
static const char *
pixel_problem (const uint32_t *expected, cairo_surface_t *surface,
	       char *text, size_t size)
{
  int width = cairo_image_surface_get_width (surface);
  int height = cairo_image_surface_get_height (surface);
  cairo_surface_flush (surface);
  const unsigned char *data = cairo_image_surface_get_data (surface);
  int stride = cairo_image_surface_get_stride (surface);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
	uint32_t pixel;
	memcpy (&pixel, data + y * stride + x * 4, sizeof pixel);
	pixel &= 0xffffff;
	if (pixel != expected[y * width + x])
	  {
	    snprintf (text, size, "pixel %d, %d is #%06lx, not #%06lx",
		      x, y, (unsigned long) pixel,
		      (unsigned long) expected[y * width + x]);
	    return text;
	  }
      }
  return NULL;
}

/* The model.  */

/* Apply MESSAGE to MODEL.  */
static void
model_apply (struct model *model, const struct rd_message *message)
{
  const struct rd_clear_area *clear = &message->u.clear_area;
  switch (message->type)
    {
    case RD_DEFINE_FRAME:
      model_define (model, message->u.define_frame.frame,
		    message->u.define_frame.width,
		    message->u.define_frame.height);
      break;
    case RD_DELETE_FRAME:
      model_delete (model, message->u.delete_frame.frame);
      break;
    case RD_CLEAR_AREA:
      model_clear (model_frame (model, clear->frame),
		   (struct rect) { clear->x, clear->y, clear->width,
				   clear->height },
		   clear->color);
      break;
    case RD_COPY_AREA:
      model_copy (model_frame (model, message->u.copy_area.frame),
		  &message->u.copy_area);
      break;
    case RD_DRAW_CURSOR:
      model_cursor (model_frame (model, message->u.draw_cursor.frame),
		    &message->u.draw_cursor);
      break;
    case RD_DRAW_FRINGE:
      model_fringe (model_frame (model, message->u.draw_fringe.frame),
		    &message->u.draw_fringe);
      break;
    default:
      break;
    }
}

/* Define the frame ID with the size WIDTH, HEIGHT.  */
static void
model_define (struct model *model, uint32_t id, uint32_t width,
	      uint32_t height)
{
  if (width > RC_MAX_FRAME_SIZE || height > RC_MAX_FRAME_SIZE)
    {
      model_delete (model, id);
      return;
    }
  struct model_frame *old = model_frame (model, id);
  struct model_frame frame = { true, width, height,
			       calloc ((size_t) width * height + 1,
				       sizeof *frame.pixels) };
  if (old)
    for (int y = 0; y < old->height; y++)
      for (int x = 0; x < old->width; x++)
	model_set (&frame, x, y, old->pixels[y * old->width + x]);
  model_delete (model, id);
  for (int i = 0; i < NFRAMES; i++)
    if (frame_ids[i] == id)
      model->frames[i] = frame;
}

/* Delete the frame ID, if it exists.  */
static void
model_delete (struct model *model, uint32_t id)
{
  for (int i = 0; i < NFRAMES; i++)
    if (frame_ids[i] == id)
      {
	free (model->frames[i].pixels);
	model->frames[i] = (struct model_frame) { 0 };
      }
}

/* Set each pixel of FRAME in RECT to COLOR.  */
static void
model_clear (struct model_frame *frame, struct rect rect, uint32_t color)
{
  for (int y = 0; frame && y < frame->height; y++)
    for (int x = 0; x < frame->width; x++)
      if (inside_rect (rect, x, y))
	model_set (frame, x, y, color);
}

/* Apply the copy-area message COPY to FRAME.  */
static void
model_copy (struct model_frame *frame, const struct rd_copy_area *copy)
{
  if (!frame)
    return;
  size_t size = (size_t) frame->width * frame->height + 1;
  uint32_t *old = malloc (size * sizeof *old);
  memcpy (old, frame->pixels, size * sizeof *old);
  struct rect source = { copy->x, copy->y, copy->width, copy->height };
  int64_t dx = (int64_t) copy->to_x - copy->x;
  int64_t dy = (int64_t) copy->to_y - copy->y;
  for (int y = 0; y < frame->height; y++)
    for (int x = 0; x < frame->width; x++)
      {
	int64_t from_x = x - dx, from_y = y - dy;
	if (inside_rect (source, from_x, from_y)
	    && inside_frame (frame, from_x, from_y))
	  frame->pixels[y * frame->width + x]
	    = old[from_y * frame->width + from_x];
      }
  free (old);
}

/* Apply the draw-cursor message CURSOR to FRAME.  */
static void
model_cursor (struct model_frame *frame,
	      const struct rd_draw_cursor *cursor)
{
  struct rect rect = { cursor->x, cursor->y, cursor->width,
		       cursor->height };
  switch (cursor->kind)
    {
    case RD_CURSOR_BOX:
    case RD_CURSOR_BAR:
    case RD_CURSOR_HBAR:
      model_clear (frame, rect, cursor->color);
      break;
    case RD_CURSOR_HOLLOW_BOX:
      model_hollow_box (frame, rect, cursor->color);
      break;
    default:
      break;
    }
}

/* Set each pixel of FRAME at an edge of RECT to COLOR.  */
static void
model_hollow_box (struct model_frame *frame, struct rect rect,
		  uint32_t color)
{
  for (int y = 0; frame && y < frame->height; y++)
    for (int x = 0; x < frame->width; x++)
      if (inside_rect (rect, x, y)
	  && (x == rect.x || x == rect.x + rect.width - 1
	      || y == rect.y || y == rect.y + rect.height - 1))
	model_set (frame, x, y, color);
}

/* Apply the draw-fringe message FRINGE to FRAME.  */
static void
model_fringe (struct model_frame *frame,
	      const struct rd_draw_fringe *fringe)
{
  struct rect bitmap = { fringe->x, fringe->y, fringe->width,
			 fringe->nrows };
  for (int y = 0; frame && y < frame->height; y++)
    for (int x = 0; x < frame->width; x++)
      if (inside_rect (bitmap, x, y))
	{
	  int column = x - fringe->x, row = y - fringe->y;
	  if (fringe_bit (fringe->rows[row], fringe->width, column))
	    model_set (frame, x, y, fringe->foreground);
	  else if (!fringe->overlay)
	    model_set (frame, x, y, fringe->background);
	}
}

/* Return the bit of the pixel COLUMN of a row BITS of a fringe of
   WIDTH pixels.  Bit 0 is the rightmost pixel.  This is a copy of the
   function of the renderer on purpose: the model must not use the
   code that it tests.  The examples check both copies.  */
static int
fringe_bit (uint16_t bits, int width, int column)
{
  int bit = width - 1 - column;
  return bit < 16 ? (bits >> bit) & 1 : 0;
}

/* Return the frame ID of MODEL, or NULL if it is not defined.  */
static struct model_frame *
model_frame (struct model *model, uint32_t id)
{
  for (int i = 0; i < NFRAMES; i++)
    if (frame_ids[i] == id && model->frames[i].defined)
      return &model->frames[i];
  return NULL;
}

/* Return true if the pixel X, Y is in RECT.  */
static bool
inside_rect (struct rect rect, int64_t x, int64_t y)
{
  return (rect.x <= x && x < rect.x + rect.width
	  && rect.y <= y && y < rect.y + rect.height);
}

/* Return true if the pixel X, Y is in FRAME.  */
static bool
inside_frame (const struct model_frame *frame, int64_t x, int64_t y)
{
  return 0 <= x && x < frame->width && 0 <= y && y < frame->height;
}

/* Set the pixel X, Y of FRAME to COLOR, if the pixel is in FRAME.  */
static void
model_set (struct model_frame *frame, int64_t x, int64_t y,
	   uint32_t color)
{
  if (inside_frame (frame, x, y))
    frame->pixels[y * frame->width + x] = color & 0xffffff;
}

static void
model_free (struct model *model)
{
  for (int i = 0; i < NFRAMES; i++)
    model_delete (model, frame_ids[i]);
}

/* The generators.  */

/* Return a random message.  A message with a frame is for a frame of
   frame_ids.  */
static struct rd_message
random_message (uint64_t *state)
{
  struct rd_message m = { 0 };
  uint32_t frame = frame_ids[random_below (state, NFRAMES)];
  uint32_t color = random_next (state);
  switch (random_below (state, 10))
    {
    case 0:
      return random_define_frame (state, frame);
    case 1:
      m.type = RD_DELETE_FRAME;
      m.u.delete_frame.frame = frame;
      return m;
    case 2:
    case 3:
      m.type = RD_CLEAR_AREA;
      m.u.clear_area = (struct rd_clear_area)
	{ frame, random_position (state), random_position (state),
	  random_size (state), random_size (state), color };
      return m;
    case 4:
    case 5:
      return random_copy_area (state, frame);
    case 6:
      m.type = RD_DRAW_CURSOR;
      m.u.draw_cursor = (struct rd_draw_cursor)
	{ frame, random_position (state), random_position (state),
	  random_size (state), random_size (state),
	  (random_below (state, 8) == 0 ? random_below (state, 256)
	   : random_below (state, RD_CURSOR_HBAR + 2)),
	  color };
      return m;
    case 7:
    case 8:
      return random_draw_fringe (state, frame, color);
    default:
      if (random_below (state, 2))
	return random_define_font (state);
      m.type = RD_HELLO;
      m.u.hello.version = random_next (state);
      return m;
    }
}

/* Return a random define-frame message for FRAME.  Some sides are at
   the limit RC_MAX_FRAME_SIZE or just above it, with a small other
   side.  */
static struct rd_message
random_define_frame (uint64_t *state, uint32_t frame)
{
  struct rd_message m = { .type = RD_DEFINE_FRAME };
  m.u.define_frame.frame = frame;
  if (random_below (state, 8) == 0)
    {
      uint32_t large = RC_MAX_FRAME_SIZE + random_below (state, 2);
      uint32_t small = random_below (state, 3);
      bool wide = random_below (state, 2);
      m.u.define_frame.width = wide ? large : small;
      m.u.define_frame.height = wide ? small : large;
    }
  else
    {
      m.u.define_frame.width = random_frame_size (state);
      m.u.define_frame.height = random_frame_size (state);
    }
  return m;
}

/* Return a random copy-area message for FRAME.  Some copies have the
   same source and destination.  */
static struct rd_message
random_copy_area (uint64_t *state, uint32_t frame)
{
  struct rd_message m = { .type = RD_COPY_AREA };
  struct rd_copy_area *copy = &m.u.copy_area;
  *copy = (struct rd_copy_area)
    { frame, random_position (state), random_position (state),
      random_size (state), random_size (state), random_position (state),
      random_position (state) };
  if (random_below (state, 8) == 0)
    {
      copy->to_x = copy->x;
      copy->to_y = copy->y;
    }
  return m;
}

/* Return a random draw-fringe message for FRAME with the foreground
   COLOR.  Some fringes are wider than 16 pixels, and some have the
   maximum number of rows.  */
static struct rd_message
random_draw_fringe (uint64_t *state, uint32_t frame, uint32_t color)
{
  static const uint16_t wide[] = { 16, 17, 255, UINT16_MAX };
  struct rd_message m = { .type = RD_DRAW_FRINGE };
  struct rd_draw_fringe *fringe = &m.u.draw_fringe;
  fringe->frame = frame;
  fringe->x = random_position (state);
  fringe->y = random_position (state);
  fringe->width = (random_below (state, 8) == 0
		   ? wide[random_below (state, 4)]
		   : random_below (state, 20));
  fringe->overlay = random_flag (state);
  fringe->cursor = random_flag (state);
  fringe->foreground = color;
  fringe->background = random_next (state);
  fringe->nrows = (random_below (state, 8) == 0 ? RD_MAX_FRINGE_ROWS
		   : random_below (state, 20));
  for (int row = 0; row < fringe->nrows; row++)
    fringe->rows[row] = random_next (state);
  return m;
}

/* Return a random define-font message.  */
static struct rd_message
random_define_font (uint64_t *state)
{
  struct rd_message m = { .type = RD_DEFINE_FONT };
  struct rd_define_font *font = &m.u.define_font;
  font->font = random_next (state);
  font->index = random_next (state);
  font->size = random_next (state);
  font->nhash = random_below (state, RD_HASH_SIZE + 1);
  for (int i = 0; i < font->nhash; i++)
    font->hash[i] = random_next (state);
  return m;
}

/* Return a random side of a frame.  Some sides are 0, and some are
   far above RC_MAX_FRAME_SIZE.  */
static uint32_t
random_frame_size (uint64_t *state)
{
  if (random_below (state, 16) == 0)
    return RC_MAX_FRAME_SIZE + 1 + random_below (state, UINT32_MAX
						  - RC_MAX_FRAME_SIZE);
  return random_below (state, TYPICAL_SIZE + 1);
}

/* Return a random value of a flag field.  Most values are 0 or 1, and
   some are other values of the field.  */
static uint8_t
random_flag (uint64_t *state)
{
  return random_below (state, random_below (state, 4) == 0 ? 256 : 2);
}

/* Return a random position.  Most positions are near a typical
   frame, and some are at the limits of the field.  */
static int32_t
random_position (uint64_t *state)
{
  switch (random_below (state, 16))
    {
    case 0:
      return INT32_MIN;
    case 1:
      return INT32_MAX;
    default:
      return (int32_t) random_below (state, TYPICAL_SIZE + 17) - 8;
    }
}

/* Return a random size of a rectangle.  Some sizes are 0, and some are
   the maximum of the field.  */
static uint32_t
random_size (uint64_t *state)
{
  if (random_below (state, 16) == 0)
    return UINT32_MAX;
  return random_below (state, TYPICAL_SIZE + 9);
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
