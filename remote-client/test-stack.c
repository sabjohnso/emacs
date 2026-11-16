/* Tests of the stack module of the remote display client.

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

/* The test is a property with a model.  A trial applies random
   operations to a stack and to a model: place a frame, forget a frame
   (and its size, as when the server destroys it), give a frame a size
   (as a define-frame message does), and reset.  The Z values of a
   trial are all different.  After each operation, these laws hold for
   each frame R as root:

   1. Hit test: rc_stack_frame_at gives a frame that the model shows
      in R and whose clip contains the point.  No later layer contains
      the point.  If no frame is given, no layer contains the point.
   2. Translation: the point of the hit plus the origin of the frame
      in R is the point in R.
   3. Paint order: the layers are the frames that the model shows in
      R, each one time, with the origin and the clip of the model.  The
      first layer is R.  The parent of each other layer is the layer
      before it or an ancestor of that layer.  Frames with the same
      parent come in the order of Z.
   4. Idempotence: a second stack that gets each place two times has
      the same parents, layers and hits.
   5. Forget: after rc_stack_forget of F, F is in no layer and gets no
      hit.
   6. No place: a frame with no place has no parent.
   7. Reset: after rc_stack_reset, no frame has a parent.
   8. Root: rc_stack_root of F is the last frame of the chain of
      parents of F, or 0 if the chain has a loop.
   9. Visibility: rc_stack_visible_p of F is the visibility of the
      last place of F, or true if F has no place.
   10. Origin: rc_stack_origin of F in R gives the origin of the layer
       of F in R if the model shows F in R, and else fails.

   The model shows a frame F in R if F, its parent and each other
   ancestor up to R are visible and have a size, and R has no parent.

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

#include "stack.h"

enum { TRIALS = 300, STEPS = 100, POINTS = 40 };

/* The frames of a trial are 1 to NFRAMES.  The frame UNKNOWN never
   has a place or a size.  */
enum { NFRAMES = 6, UNKNOWN = 9, MAX_LAYERS = 16 };

/* The place of one frame in the model.  */
struct model_place
{
  bool placed;
  uint32_t parent;
  int32_t x;
  int32_t y;
  uint32_t z;
  bool visible;
};

/* The model: the places and the sizes of the frames, by frame
   number.  */
struct model
{
  struct model_place place[NFRAMES + 1];
  bool sized[NFRAMES + 1];
  int width[NFRAMES + 1];
  int height[NFRAMES + 1];
};

/* The layers of one root, as rc_stack_each_layer gives them.  */
struct layers
{
  struct rc_layer layer[MAX_LAYERS];
  int count;
  bool overflow;
};

static bool run_trial (uint64_t);
static void apply_random_operation (struct model *, struct rc_stack *,
				    struct rc_stack *, uint64_t *, int);
static bool laws_hold_p (const struct model *, struct rc_stack *,
			 struct rc_stack *, uint64_t *);
static bool root_laws_hold_p (const struct model *, struct rc_stack *,
			      struct rc_stack *, uint32_t, uint64_t *);
static bool layers_match_model_p (const struct model *,
				  const struct layers *, uint32_t);
static bool order_holds_p (const struct model *, const struct layers *);
static bool hit_holds_p (const struct model *, const struct rc_stack *,
			 const struct layers *, uint32_t, int, int);
static bool same_hits_p (const struct rc_stack *, const struct rc_stack *,
			 uint32_t, int, int);
static bool same_layers_p (const struct layers *, const struct layers *);
static bool same_layer_p (const struct rc_layer *, const struct rc_layer *);
static bool model_shows_p (const struct model *, uint32_t, uint32_t,
			   struct rc_layer *);
static uint32_t model_parent (const struct model *, uint32_t);
static uint32_t model_root (const struct model *, uint32_t);
static bool model_visible_p (const struct model *, uint32_t);
static bool origin_holds_p (const struct model *, const struct rc_stack *,
			    uint32_t, uint32_t);
static bool model_ancestor_p (const struct model *, uint32_t, uint32_t);
static bool sibling_before_p (const struct model *, uint32_t, uint32_t);
static struct rc_rect intersect (int64_t, int64_t, int64_t, int64_t,
				 const struct rc_rect *);
static bool rect_contains_p (const struct rc_rect *, int, int);
static void get_layers (const struct rc_stack *, uint32_t,
			struct layers *);
static void add_layer (const struct rc_layer *, void *);
static bool model_size (void *, uint32_t, int *, int *);
static bool run_examples (void);
static bool example_unknown_parent (void);
static bool example_hidden_ancestor (void);
static bool example_equal_z (void);
static bool equal_z_holds_p (bool);
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
	fprintf (stderr, "test-stack: seed %llu failed\n",
		 (unsigned long long) (base + trial));
	return 1;
      }
  if (!run_examples ())
    return 1;
  printf ("test-stack: %d trials and the examples passed\n", TRIALS);
  return 0;
}

/* Run the trial of SEED.  Return true if it passes.  */
static bool
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  struct model model;
  memset (&model, 0, sizeof model);
  for (uint32_t frame = 1; frame <= NFRAMES; frame++)
    {
      model.sized[frame] = random_below (&state, 6) != 0;
      model.width[frame] = 1 + random_below (&state, 50);
      model.height[frame] = 1 + random_below (&state, 50);
    }
  struct rc_stack *stack = rc_stack_new (model_size, &model);
  struct rc_stack *twice = rc_stack_new (model_size, &model);
  bool passed = laws_hold_p (&model, stack, twice, &state);
  for (int step = 0; passed && step < STEPS; step++)
    {
      apply_random_operation (&model, stack, twice, &state, step);
      passed = laws_hold_p (&model, stack, twice, &state);
    }
  rc_stack_free (stack);
  rc_stack_free (twice);
  return passed;
}

/* Apply a random operation to MODEL, STACK and TWICE.  TWICE gets each
   place two times.  STEP makes each Z value different.  */
static void
apply_random_operation (struct model *model, struct rc_stack *stack,
			struct rc_stack *twice, uint64_t *state, int step)
{
  uint32_t frame = 1 + random_below (state, NFRAMES);
  uint64_t kind = random_below (state, 40);
  if (kind < 28)
    {
      uint64_t choice = random_below (state, NFRAMES + 4);
      uint32_t parent = (choice <= NFRAMES ? choice
			 : choice == NFRAMES + 1 ? UNKNOWN : 0);
      struct rd_place_frame place
	= place_of (frame, parent, random_position (state),
		    random_position (state),
		    random_below (state, 1000) * STEPS + step,
		    random_below (state, 4) != 0);
      rc_stack_place (stack, &place);
      rc_stack_place (twice, &place);
      rc_stack_place (twice, &place);
      model->place[frame] = (struct model_place) {
	true, place.parent, place.x, place.y, place.z, place.visible };
    }
  else if (kind < 33)
    {
      rc_stack_forget (stack, frame);
      rc_stack_forget (twice, frame);
      model->place[frame].placed = false;
      model->sized[frame] = false;
    }
  else if (kind < 39)
    {
      model->sized[frame] = true;
      model->width[frame] = 1 + random_below (state, 50);
      model->height[frame] = 1 + random_below (state, 50);
    }
  else
    {
      rc_stack_reset (stack);
      rc_stack_reset (twice);
      for (uint32_t f = 1; f <= NFRAMES; f++)
	model->place[f].placed = false;
    }
}

/* Return true if the laws hold for STACK, TWICE and MODEL, with each
   frame as root.  */
static bool
laws_hold_p (const struct model *model, struct rc_stack *stack,
	     struct rc_stack *twice, uint64_t *state)
{
  for (uint32_t frame = 1; frame <= UNKNOWN; frame++)
    {
      uint32_t parent = model_parent (model, frame);
      if (rc_stack_parent (stack, frame) != parent
	  || rc_stack_parent (twice, frame) != parent
	  || rc_stack_root (stack, frame) != model_root (model, frame)
	  || rc_stack_visible_p (stack, frame) != model_visible_p (model,
								   frame)
	  || !root_laws_hold_p (model, stack, twice, frame, state))
	return false;
    }
  return true;
}

/* Return true if the laws hold for the layers and the hits of ROOT.  */
static bool
root_laws_hold_p (const struct model *model, struct rc_stack *stack,
		  struct rc_stack *twice, uint32_t root, uint64_t *state)
{
  struct layers layers, twice_layers;
  get_layers (stack, root, &layers);
  get_layers (twice, root, &twice_layers);
  if (!layers_match_model_p (model, &layers, root)
      || !order_holds_p (model, &layers)
      || !same_layers_p (&layers, &twice_layers))
    return false;
  for (uint32_t frame = 1; frame <= UNKNOWN; frame++)
    if (!origin_holds_p (model, stack, root, frame))
      return false;
  for (int i = 0; i < POINTS; i++)
    {
      int x = (int) random_below (state, 70) - 10;
      int y = (int) random_below (state, 70) - 10;
      if (!hit_holds_p (model, stack, &layers, root, x, y)
	  || !same_hits_p (stack, twice, root, x, y))
	return false;
    }
  return true;
}

/* Law 3, first part: return true if LAYERS are the frames that MODEL
   shows in ROOT, each one time, with the origin and the clip of
   MODEL, and the first layer is ROOT.  */
static bool
layers_match_model_p (const struct model *model,
		      const struct layers *layers, uint32_t root)
{
  if (layers->overflow
      || (layers->count > 0 && layers->layer[0].frame != root))
    return false;
  int shown = 0;
  for (uint32_t frame = 1; frame <= UNKNOWN; frame++)
    {
      struct rc_layer expected;
      if (!model_shows_p (model, root, frame, &expected))
	continue;
      shown++;
      int found = 0;
      for (int i = 0; i < layers->count; i++)
	if (layers->layer[i].frame == frame)
	  {
	    found++;
	    if (!same_layer_p (&layers->layer[i], &expected))
	      return false;
	  }
      if (found != 1)
	return false;
    }
  return layers->count == shown;
}

/* Law 3, second part: return true if the parent of each layer after
   the first is the layer before it or an ancestor of that layer, and
   if frames with the same parent come in the order of Z.  */
static bool
order_holds_p (const struct model *model, const struct layers *layers)
{
  for (int i = 1; i < layers->count; i++)
    {
      uint32_t parent = model_parent (model, layers->layer[i].frame);
      uint32_t before = layers->layer[i - 1].frame;
      if (parent != before && !model_ancestor_p (model, parent, before))
	return false;
      for (int k = 0; k < i; k++)
	if (model_parent (model, layers->layer[k].frame) == parent
	    && !sibling_before_p (model, layers->layer[k].frame,
				  layers->layer[i].frame))
	  return false;
    }
  return true;
}

/* Laws 1 and 2: return true if the hit of X, Y in ROOT is the last
   layer whose clip contains the point, with the point in the
   coordinates of that frame.  */
static bool
hit_holds_p (const struct model *model, const struct rc_stack *stack,
	     const struct layers *layers, uint32_t root, int x, int y)
{
  int last = -1;
  for (int i = 0; i < layers->count; i++)
    if (rect_contains_p (&layers->layer[i].clip, x, y))
      last = i;
  struct rc_hit hit;
  bool found = rc_stack_frame_at (stack, root, x, y, &hit);
  if (last < 0)
    return !found;
  struct rc_layer expected;
  return (found && hit.frame == layers->layer[last].frame
	  && model_shows_p (model, root, hit.frame, &expected)
	  && rect_contains_p (&expected.clip, x, y)
	  && hit.x + expected.x == x && hit.y + expected.y == y);
}

/* Law 4, for the hits: return true if STACK and TWICE give the same
   hit for X, Y in ROOT.  */
static bool
same_hits_p (const struct rc_stack *stack, const struct rc_stack *twice,
	     uint32_t root, int x, int y)
{
  struct rc_hit a, b;
  bool found_a = rc_stack_frame_at (stack, root, x, y, &a);
  bool found_b = rc_stack_frame_at (twice, root, x, y, &b);
  return (found_a == found_b
	  && (!found_a
	      || (a.frame == b.frame && a.x == b.x && a.y == b.y)));
}

/* Law 4, for the layers: return true if A and B are equal.  */
static bool
same_layers_p (const struct layers *a, const struct layers *b)
{
  if (a->count != b->count || a->overflow != b->overflow)
    return false;
  for (int i = 0; i < a->count; i++)
    if (!same_layer_p (&a->layer[i], &b->layer[i]))
      return false;
  return true;
}

/* Return true if the layers A and B are equal.  */
static bool
same_layer_p (const struct rc_layer *a, const struct rc_layer *b)
{
  return (a->frame == b->frame && a->x == b->x && a->y == b->y
	  && a->clip.x == b->clip.x && a->clip.y == b->clip.y
	  && a->clip.width == b->clip.width
	  && a->clip.height == b->clip.height);
}

/* Return true if MODEL shows FRAME in ROOT, and put the layer of
   FRAME in LAYER.  */
static bool
model_shows_p (const struct model *model, uint32_t root, uint32_t frame,
	       struct rc_layer *layer)
{
  /* The chain from FRAME up to ROOT.  A longer chain has a loop.  */
  uint32_t chain[NFRAMES + 1];
  int length = 0;
  for (uint32_t f = frame; f != 0; f = model_parent (model, f))
    {
      if (length == NFRAMES || f > NFRAMES || !model->sized[f]
	  || (model->place[f].placed && !model->place[f].visible))
	return false;
      chain[length++] = f;
    }
  if (chain[length - 1] != root)
    return false;
  memset (layer, 0, sizeof *layer);
  layer->frame = frame;
  layer->clip = (struct rc_rect) { 0, 0, model->width[root],
				   model->height[root] };
  for (int i = length - 2; i >= 0; i--)
    {
      const struct model_place *place = &model->place[chain[i]];
      layer->x += place->x;
      layer->y += place->y;
      layer->clip = intersect (layer->x, layer->y, model->width[chain[i]],
			       model->height[chain[i]], &layer->clip);
    }
  return true;
}

/* Return the parent of FRAME in MODEL, or 0.  */
static uint32_t
model_parent (const struct model *model, uint32_t frame)
{
  return (frame <= NFRAMES && model->place[frame].placed
	  ? model->place[frame].parent : 0);
}

/* Law 8: return the last frame of the chain of parents of FRAME in
   MODEL, or 0 if the chain has a loop.  */
static uint32_t
model_root (const struct model *model, uint32_t frame)
{
  uint32_t f = frame;
  for (int i = 0; i <= NFRAMES; i++)
    {
      uint32_t parent = model_parent (model, f);
      if (parent == 0)
	return f;
      f = parent;
    }
  return 0;
}

/* Law 9: return the visibility of the last place of FRAME in MODEL, or
   true if FRAME has no place.  */
static bool
model_visible_p (const struct model *model, uint32_t frame)
{
  return (frame > NFRAMES || !model->place[frame].placed
	  || model->place[frame].visible);
}

/* Law 10: return true if rc_stack_origin of FRAME in ROOT agrees with
   the layer of FRAME that MODEL shows in ROOT.  */
static bool
origin_holds_p (const struct model *model, const struct rc_stack *stack,
		uint32_t root, uint32_t frame)
{
  struct rc_layer expected;
  int64_t x, y;
  bool found = rc_stack_origin (stack, root, frame, &x, &y);
  if (!model_shows_p (model, root, frame, &expected))
    return !found;
  return found && x == expected.x && y == expected.y;
}

/* Return true if ANCESTOR is the parent of FRAME in MODEL, or an
   ancestor of that parent.  */
static bool
model_ancestor_p (const struct model *model, uint32_t ancestor,
		  uint32_t frame)
{
  uint32_t f = model_parent (model, frame);
  for (int i = 0; f != 0 && i <= NFRAMES; i++, f = model_parent (model, f))
    if (f == ancestor)
      return true;
  return false;
}

/* Return true if the sibling A comes before the sibling B: A has a
   smaller Z, or the same Z and a smaller number.  */
static bool
sibling_before_p (const struct model *model, uint32_t a, uint32_t b)
{
  uint32_t za = model->place[a].z, zb = model->place[b].z;
  return za < zb || (za == zb && a < b);
}

/* Return the part of the rectangle X, Y, WIDTH, HEIGHT that is in
   CLIP.  An empty part is all 0.  */
static struct rc_rect
intersect (int64_t x, int64_t y, int64_t width, int64_t height,
	   const struct rc_rect *clip)
{
  int64_t left = x > clip->x ? x : clip->x;
  int64_t top = y > clip->y ? y : clip->y;
  int64_t right = x + width < clip->x + clip->width
    ? x + width : clip->x + clip->width;
  int64_t bottom = y + height < clip->y + clip->height
    ? y + height : clip->y + clip->height;
  if (left >= right || top >= bottom)
    return (struct rc_rect) { 0, 0, 0, 0 };
  return (struct rc_rect) { left, top, right - left, bottom - top };
}

/* Return true if RECT contains the point X, Y.  */
static bool
rect_contains_p (const struct rc_rect *rect, int x, int y)
{
  return (x >= rect->x && x - rect->x < rect->width
	  && y >= rect->y && y - rect->y < rect->height);
}

/* Put the layers of ROOT in STACK in LAYERS.  */
static void
get_layers (const struct rc_stack *stack, uint32_t root,
	    struct layers *layers)
{
  memset (layers, 0, sizeof *layers);
  rc_stack_each_layer (stack, root, add_layer, layers);
}

/* The function of rc_stack_each_layer: add LAYER to the struct layers
   DATA.  */
static void
add_layer (const struct rc_layer *layer, void *data)
{
  struct layers *layers = data;
  if (layers->count == MAX_LAYERS)
    layers->overflow = true;
  else
    layers->layer[layers->count++] = *layer;
}

/* The size function of the stacks of a trial.  DATA is the model.  */
static bool
model_size (void *data, uint32_t frame, int *width, int *height)
{
  const struct model *model = data;
  if (frame == 0 || frame > NFRAMES || !model->sized[frame])
    return false;
  *width = model->width[frame];
  *height = model->height[frame];
  return true;
}

/* Run the examples.  Return true if they pass.  */
static bool
run_examples (void)
{
  bool passed = true;
  if (!example_unknown_parent ())
    {
      fprintf (stderr, "test-stack: unknown parent example failed\n");
      passed = false;
    }
  if (!example_hidden_ancestor ())
    {
      fprintf (stderr, "test-stack: hidden ancestor example failed\n");
      passed = false;
    }
  if (!example_equal_z ())
    {
      fprintf (stderr, "test-stack: equal Z example failed\n");
      passed = false;
    }
  return passed;
}

/* A model in which frames 1 to 3 have the size 100 by 100.  */
static struct model
example_model (void)
{
  struct model model;
  memset (&model, 0, sizeof model);
  for (uint32_t frame = 1; frame <= 3; frame++)
    {
      model.sized[frame] = true;
      model.width[frame] = 100;
      model.height[frame] = 100;
    }
  return model;
}

/* A child whose parent the client does not know is not painted.  */
static bool
example_unknown_parent (void)
{
  struct model model = example_model ();
  struct rc_stack *stack = rc_stack_new (model_size, &model);
  struct rd_place_frame place = place_of (2, UNKNOWN, 0, 0, 1, true);
  rc_stack_place (stack, &place);
  struct layers root, unknown, child;
  get_layers (stack, 1, &root);
  get_layers (stack, UNKNOWN, &unknown);
  get_layers (stack, 2, &child);
  bool passed = (root.count == 1 && unknown.count == 0
		 && child.count == 0
		 && rc_stack_parent (stack, 2) == UNKNOWN);
  rc_stack_free (stack);
  return passed;
}

/* A visible child of a hidden child is not painted and gets no
   hit.  */
static bool
example_hidden_ancestor (void)
{
  struct model model = example_model ();
  struct rc_stack *stack = rc_stack_new (model_size, &model);
  struct rd_place_frame hidden = place_of (2, 1, 0, 0, 1, false);
  struct rd_place_frame child = place_of (3, 2, 0, 0, 1, true);
  rc_stack_place (stack, &hidden);
  rc_stack_place (stack, &child);
  struct layers layers;
  get_layers (stack, 1, &layers);
  struct rc_hit hit;
  bool passed = (layers.count == 1
		 && rc_stack_frame_at (stack, 1, 5, 5, &hit)
		 && hit.frame == 1);
  rc_stack_free (stack);
  return passed;
}

/* Of two siblings with equal Z, the larger frame number is above, in
   either order of the messages.  */
static bool
example_equal_z (void)
{
  return equal_z_holds_p (true) && equal_z_holds_p (false);
}

/* Return true if the frame 3 is above the frame 2 with equal Z.  If
   UPPER_FIRST, the place of the frame 3 comes first.  */
static bool
equal_z_holds_p (bool upper_first)
{
  struct model model = example_model ();
  struct rc_stack *stack = rc_stack_new (model_size, &model);
  struct rd_place_frame upper = place_of (3, 1, 10, 20, 5, true);
  struct rd_place_frame lower = place_of (2, 1, 10, 20, 5, true);
  rc_stack_place (stack, upper_first ? &upper : &lower);
  rc_stack_place (stack, upper_first ? &lower : &upper);
  struct layers layers;
  get_layers (stack, 1, &layers);
  struct rc_hit hit;
  bool passed = (layers.count == 3 && layers.layer[1].frame == 2
		 && layers.layer[2].frame == 3
		 && rc_stack_frame_at (stack, 1, 15, 25, &hit)
		 && hit.frame == 3 && hit.x == 5 && hit.y == 5);
  rc_stack_free (stack);
  return passed;
}

/* Return a place-frame message with these fields.  */
static struct rd_place_frame
place_of (uint32_t frame, uint32_t parent, int32_t x, int32_t y,
	  uint32_t z, bool visible)
{
  return (struct rd_place_frame) { .frame = frame, .parent = parent,
				   .x = x, .y = y, .z = z,
				   .visible = visible };
}

/* Return a random position: mostly a small one, and sometimes the
   smallest or the largest value of the field.  */
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
      return (int32_t) random_below (state, 80) - 20;
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
