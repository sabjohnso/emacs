/* Tests of the compose module of the remote display client.

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

/* The test is a property.  A trial makes random frames: each frame
   has a surface of a random size, filled with one color, and a random
   place.  Some frames have no surface.  Then, for each frame R as
   root, this law holds:

   Agreement: rc_compose of R paints, at each point of the target, the
   pixel of the surface of the frame that rc_stack_frame_at gives for
   that point, at the point in that frame.  If no frame contains the
   point, the pixel of the target does not change.

   Thus the user clicks the frame that the user sees.

   With no argument, the program runs TRIALS trials from a seed that
   it gets from the clock and the process ID.  With the argument SEED,
   it runs only the trial of that seed.  Then it runs the examples.  */

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cairo.h>

#include "compose.h"

enum { TRIALS = 300 };

/* The frames of a trial are 1 to NFRAMES.  The frame UNKNOWN never
   has a place or a surface.  The target is TARGET pixels on each side,
   so it covers more than each root.  */
enum { NFRAMES = 6, UNKNOWN = 9, MAX_SIDE = 40, TARGET = 64 };

/* The color of the target before rc_compose.  No frame has this
   color.  */
enum { BACKGROUND = 0x000000 };

/* The frames of a test: the surface and the size of each frame, by
   frame number.  A frame can have a size but no surface.  */
struct world
{
  cairo_surface_t *surface[NFRAMES + 1];
  bool sized[NFRAMES + 1];
  int width[NFRAMES + 1];
  int height[NFRAMES + 1];
};

static bool run_trial (uint64_t);
static void make_random_world (struct world *, struct rc_stack *,
			       uint64_t *);
static struct rd_place_frame random_place (uint32_t, uint64_t *);
static bool agreement_holds_p (const struct rc_stack *, struct world *,
			       uint32_t);
static uint32_t expected_pixel (const struct rc_stack *, struct world *,
				uint32_t, int, int);
static cairo_surface_t *compose_root (const struct rc_stack *,
				      struct world *, uint32_t);
static bool run_examples (void);
static bool example_child_clipped_to_parent (void);
static bool example_hidden_frame (void);
static bool example_frame_with_no_surface (void);
static bool composed_pixel_is_p (const struct rc_stack *, struct world *,
				 uint32_t, int, int, uint32_t);
static void give_surface (struct world *, uint32_t, int, int);
static void give_size (struct world *, uint32_t, int, int);
static void free_world (struct world *);
static uint32_t color_of (uint32_t);
static uint32_t pixel_at (cairo_surface_t *, int, int);
static bool world_size (void *, uint32_t, int *, int *);
static cairo_surface_t *world_surface (void *, uint32_t);
static struct rd_place_frame place_of (uint32_t, uint32_t, int32_t,
				       int32_t, uint32_t, bool);
static int32_t random_position (uint64_t *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 0 : 1;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    if (!run_trial (base + trial))
      {
	fprintf (stderr, "test-compose: seed %llu failed\n",
		 (unsigned long long) (base + trial));
	return 1;
      }
  if (!run_examples ())
    return 1;
  printf ("test-compose: %d trials and the examples passed\n", TRIALS);
  return 0;
}

/* Run the trial of SEED.  Return true if the law holds for each
   root.  */
static bool
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  struct world world;
  memset (&world, 0, sizeof world);
  struct rc_stack *stack = rc_stack_new (world_size, &world);
  make_random_world (&world, stack, &state);
  bool passed = true;
  for (uint32_t root = 1; passed && root <= UNKNOWN; root++)
    passed = agreement_holds_p (stack, &world, root);
  rc_stack_free (stack);
  free_world (&world);
  return passed;
}

/* Give each frame of WORLD a random surface or no surface, and a
   random place in STACK or no place.  The Z values are all
   different.  */
static void
make_random_world (struct world *world, struct rc_stack *stack,
		   uint64_t *state)
{
  for (uint32_t frame = 1; frame <= NFRAMES; frame++)
    {
      if (random_below (state, 6) != 0)
	give_surface (world, frame, 1 + random_below (state, MAX_SIDE),
		      1 + random_below (state, MAX_SIDE));
      if (random_below (state, 4) != 0)
	{
	  struct rd_place_frame place = random_place (frame, state);
	  rc_stack_place (stack, &place);
	}
    }
}

/* Return a random place of FRAME.  The parent can be any frame, the
   frame UNKNOWN or no frame.  */
static struct rd_place_frame
random_place (uint32_t frame, uint64_t *state)
{
  uint64_t choice = random_below (state, NFRAMES + 4);
  uint32_t parent = (choice <= NFRAMES ? choice
		     : choice == NFRAMES + 1 ? UNKNOWN : 0);
  return place_of (frame, parent, random_position (state),
		   random_position (state),
		   random_below (state, 1000) * NFRAMES + frame,
		   random_below (state, 4) != 0);
}

/* The agreement law: return true if each pixel of the composed target
   of ROOT is the pixel that the hit test of STACK gives.  */
static bool
agreement_holds_p (const struct rc_stack *stack, struct world *world,
		   uint32_t root)
{
  cairo_surface_t *target = compose_root (stack, world, root);
  bool holds = true;
  for (int y = 0; holds && y < TARGET; y++)
    for (int x = 0; holds && x < TARGET; x++)
      {
	uint32_t expected = expected_pixel (stack, world, root, x, y);
	uint32_t actual = pixel_at (target, x, y);
	if (actual != expected)
	  {
	    fprintf (stderr,
		     "test-compose: root %u at %d, %d: pixel %06x,"
		     " expected %06x\n",
		     (unsigned) root, x, y, (unsigned) actual,
		     (unsigned) expected);
	    holds = false;
	  }
      }
  cairo_surface_destroy (target);
  return holds;
}

/* Return the pixel that the hit test of STACK gives at X, Y of ROOT:
   the pixel of the surface of the frame under the point, at the point
   in that frame, or BACKGROUND if no frame is under the point.  */
static uint32_t
expected_pixel (const struct rc_stack *stack, struct world *world,
		uint32_t root, int x, int y)
{
  struct rc_hit hit;
  if (!rc_stack_frame_at (stack, root, x, y, &hit))
    return BACKGROUND;
  return pixel_at (world_surface (world, hit.frame), hit.x, hit.y);
}

/* Return a new target of TARGET pixels on each side, filled with
   BACKGROUND, with the layers of ROOT composed on it.  */
static cairo_surface_t *
compose_root (const struct rc_stack *stack, struct world *world,
	      uint32_t root)
{
  cairo_surface_t *target
    = cairo_image_surface_create (CAIRO_FORMAT_RGB24, TARGET, TARGET);
  cairo_t *cr = cairo_create (target);
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_paint (cr);
  rc_compose (cr, stack, root, world_surface, world);
  cairo_destroy (cr);
  cairo_surface_flush (target);
  return target;
}

/* Run the examples.  Return true if they pass.  */
static bool
run_examples (void)
{
  bool passed = true;
  passed &= example_child_clipped_to_parent ();
  passed &= example_hidden_frame ();
  passed &= example_frame_with_no_surface ();
  return passed;
}

/* A grandchild that extends past its parent is clipped to the parent.
   The root shows its own pixel there.  */
static bool
example_child_clipped_to_parent (void)
{
  struct world world;
  memset (&world, 0, sizeof world);
  give_surface (&world, 1, 30, 30);
  give_surface (&world, 2, 10, 10);
  give_surface (&world, 3, 10, 10);
  struct rc_stack *stack = rc_stack_new (world_size, &world);
  struct rd_place_frame child = place_of (2, 1, 0, 0, 1, true);
  struct rd_place_frame grandchild = place_of (3, 2, 5, 5, 1, true);
  rc_stack_place (stack, &child);
  rc_stack_place (stack, &grandchild);
  bool passed = (composed_pixel_is_p (stack, &world, 1, 7, 7, color_of (3))
		 && composed_pixel_is_p (stack, &world, 1, 12, 12,
					 color_of (1))
		 && composed_pixel_is_p (stack, &world, 1, 2, 2,
					 color_of (2)));
  if (!passed)
    fprintf (stderr, "test-compose: a child is not clipped to its"
	     " parent\n");
  rc_stack_free (stack);
  free_world (&world);
  return passed;
}

/* A child that is not visible is not painted, and its children are
   not painted.  */
static bool
example_hidden_frame (void)
{
  struct world world;
  memset (&world, 0, sizeof world);
  give_surface (&world, 1, 30, 30);
  give_surface (&world, 2, 20, 20);
  give_surface (&world, 3, 10, 10);
  struct rc_stack *stack = rc_stack_new (world_size, &world);
  struct rd_place_frame child = place_of (2, 1, 0, 0, 1, false);
  struct rd_place_frame grandchild = place_of (3, 2, 0, 0, 1, true);
  rc_stack_place (stack, &child);
  rc_stack_place (stack, &grandchild);
  bool passed = (composed_pixel_is_p (stack, &world, 1, 5, 5, color_of (1))
		 && composed_pixel_is_p (stack, &world, 1, 15, 15,
					 color_of (1)));
  if (!passed)
    fprintf (stderr, "test-compose: a hidden frame is painted\n");
  rc_stack_free (stack);
  free_world (&world);
  return passed;
}

/* A frame that has a size but no surface is not painted, so its
   parent shows there.  */
static bool
example_frame_with_no_surface (void)
{
  struct world world;
  memset (&world, 0, sizeof world);
  give_surface (&world, 1, 30, 30);
  give_size (&world, 2, 10, 10);
  struct rc_stack *stack = rc_stack_new (world_size, &world);
  struct rd_place_frame child = place_of (2, 1, 0, 0, 1, true);
  rc_stack_place (stack, &child);
  bool passed = composed_pixel_is_p (stack, &world, 1, 5, 5,
				     color_of (1));
  if (!passed)
    fprintf (stderr, "test-compose: a frame with no surface is"
	     " painted\n");
  rc_stack_free (stack);
  free_world (&world);
  return passed;
}

/* Return true if the composed target of ROOT has the pixel COLOR at
   X, Y.  */
static bool
composed_pixel_is_p (const struct rc_stack *stack, struct world *world,
		     uint32_t root, int x, int y, uint32_t color)
{
  cairo_surface_t *target = compose_root (stack, world, root);
  bool is = pixel_at (target, x, y) == color;
  cairo_surface_destroy (target);
  return is;
}

/* Give FRAME in WORLD a surface of WIDTH by HEIGHT pixels, filled with
   the color of FRAME.  */
static void
give_surface (struct world *world, uint32_t frame, int width, int height)
{
  cairo_surface_t *surface
    = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
  cairo_t *cr = cairo_create (surface);
  uint32_t color = color_of (frame);
  cairo_set_source_rgb (cr, (color >> 16 & 0xff) / 255.0,
			(color >> 8 & 0xff) / 255.0, (color & 0xff) / 255.0);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_flush (surface);
  world->surface[frame] = surface;
  give_size (world, frame, width, height);
}

/* Give FRAME in WORLD the size WIDTH by HEIGHT.  */
static void
give_size (struct world *world, uint32_t frame, int width, int height)
{
  world->sized[frame] = true;
  world->width[frame] = width;
  world->height[frame] = height;
}

/* Free the surfaces of WORLD.  */
static void
free_world (struct world *world)
{
  for (uint32_t frame = 1; frame <= NFRAMES; frame++)
    if (world->surface[frame])
      cairo_surface_destroy (world->surface[frame]);
}

/* Return the color of FRAME as 0xRRGGBB.  Each frame has a different
   color, and no frame has the color BACKGROUND.  */
static uint32_t
color_of (uint32_t frame)
{
  return 0x111111 * frame;
}

/* Return the pixel of SURFACE at X, Y as 0xRRGGBB.  */
static uint32_t
pixel_at (cairo_surface_t *surface, int x, int y)
{
  const unsigned char *data = cairo_image_surface_get_data (surface);
  int stride = cairo_image_surface_get_stride (surface);
  uint32_t pixel;
  memcpy (&pixel, data + (size_t) y * stride + (size_t) x * 4,
	  sizeof pixel);
  return pixel & 0xffffff;
}

/* The size function of the stack: the size of FRAME in the world
   DATA.  */
static bool
world_size (void *data, uint32_t frame, int *width, int *height)
{
  const struct world *world = data;
  if (frame < 1 || frame > NFRAMES || !world->sized[frame])
    return false;
  *width = world->width[frame];
  *height = world->height[frame];
  return true;
}

/* The surface function of rc_compose: the surface of FRAME in the
   world DATA, or NULL.  */
static cairo_surface_t *
world_surface (void *data, uint32_t frame)
{
  struct world *world = data;
  return frame < 1 || frame > NFRAMES ? NULL : world->surface[frame];
}

/* Return a place-frame message with the given fields.  */
static struct rd_place_frame
place_of (uint32_t frame, uint32_t parent, int32_t x, int32_t y,
	  uint32_t z, bool visible)
{
  struct rd_place_frame place;
  memset (&place, 0, sizeof place);
  place.frame = frame;
  place.parent = parent;
  place.x = x;
  place.y = y;
  place.z = z;
  place.visible = visible;
  return place;
}

/* Return a random position: mostly a small one, sometimes an extreme
   one.  */
static int32_t
random_position (uint64_t *state)
{
  switch (random_below (state, 20))
    {
    case 0:
      return INT32_MIN;
    case 1:
      return INT32_MAX;
    default:
      return (int32_t) random_below (state, 60) - 20;
    }
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
