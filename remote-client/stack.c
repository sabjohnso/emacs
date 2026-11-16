/* The places of the frames of the remote display client.

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

/* A server has few frames, so the stack keeps the places in an array,
   and a search is linear.  The layers of a root come from a walk of
   the tree of the root with a list of the frames to visit.  A frame
   whose chain of parents has a loop is never in the tree of a root,
   so the walk always ends.  */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include "stack.h"

struct rc_stack
{
  rc_stack_size_function size;
  void *data;
  struct rd_place_frame *places;
  int count;
  int capacity;
};

/* A frame to visit in the walk of a tree: its place, and the layer of
   its parent.  */
struct visit
{
  const struct rd_place_frame *place;
  struct rc_layer parent;
};

/* The point of rc_stack_frame_at, and the last layer that contains
   it.  */
struct search
{
  int x;
  int y;
  bool found;
  struct rc_layer layer;
};

static int find_place (const struct rc_stack *, uint32_t);
static bool root_layer (const struct rc_stack *, uint32_t,
			struct rc_layer *);
static bool child_layer (const struct rc_stack *,
			 const struct rd_place_frame *,
			 const struct rc_layer *, struct rc_layer *);
static int push_children (const struct rc_stack *,
			  const struct rc_layer *, struct visit *, int);
static int compare_visits (const void *, const void *);
static struct rc_rect intersect (int64_t, int64_t, int64_t, int64_t,
				 const struct rc_rect *);
static void note_layer (const struct rc_layer *, void *);
static bool rect_contains_p (const struct rc_rect *, int, int);
static void *xrealloc (void *, size_t);

struct rc_stack *
rc_stack_new (rc_stack_size_function size, void *data)
{
  struct rc_stack *stack = xrealloc (NULL, sizeof *stack);
  *stack = (struct rc_stack) { .size = size, .data = data };
  return stack;
}

void
rc_stack_free (struct rc_stack *stack)
{
  free (stack->places);
  free (stack);
}

void
rc_stack_place (struct rc_stack *stack, const struct rd_place_frame *place)
{
  int i = find_place (stack, place->frame);
  if (i < 0)
    {
      if (stack->count == stack->capacity)
	{
	  stack->capacity = stack->capacity ? 2 * stack->capacity : 8;
	  stack->places = xrealloc (stack->places,
				    stack->capacity * sizeof *stack->places);
	}
      i = stack->count++;
    }
  stack->places[i] = *place;
}

void
rc_stack_forget (struct rc_stack *stack, uint32_t frame)
{
  int i = find_place (stack, frame);
  if (i >= 0)
    stack->places[i] = stack->places[--stack->count];
}

void
rc_stack_reset (struct rc_stack *stack)
{
  stack->count = 0;
}

uint32_t
rc_stack_parent (const struct rc_stack *stack, uint32_t frame)
{
  int i = find_place (stack, frame);
  return i < 0 ? 0 : stack->places[i].parent;
}

/* Walk the tree of ROOT in the paint order.  Each frame of the tree
   goes into the list of visits one time, when the walk visits its
   parent, so the list needs at most one entry for each place.  The
   children of a frame go into the list with the highest first, so
   the lowest comes out first.  */
void
rc_stack_each_layer (const struct rc_stack *stack, uint32_t root,
		     void (*function) (const struct rc_layer *layer,
				       void *data),
		     void *data)
{
  struct rc_layer layer;
  if (!root_layer (stack, root, &layer))
    return;
  function (&layer, data);
  struct visit *visits = xrealloc (NULL, (stack->count + 1)
				   * sizeof *visits);
  int count = push_children (stack, &layer, visits, 0);
  while (count > 0)
    {
      struct visit visit = visits[--count];
      if (child_layer (stack, visit.place, &visit.parent, &layer))
	{
	  function (&layer, data);
	  count = push_children (stack, &layer, visits, count);
	}
    }
  free (visits);
}

bool
rc_stack_frame_at (const struct rc_stack *stack, uint32_t root,
		   int x, int y, struct rc_hit *hit)
{
  struct search search = { .x = x, .y = y };
  rc_stack_each_layer (stack, root, note_layer, &search);
  if (search.found)
    *hit = (struct rc_hit) { search.layer.frame,
			     x - search.layer.x, y - search.layer.y };
  return search.found;
}

/* Return the index of the place of FRAME in STACK, or -1.  */
static int
find_place (const struct rc_stack *stack, uint32_t frame)
{
  for (int i = 0; i < stack->count; i++)
    if (stack->places[i].frame == frame)
      return i;
  return -1;
}

/* Put the layer of ROOT in LAYER, and return true.  Return false if
   ROOT has a parent, is not visible, or has no size.  */
static bool
root_layer (const struct rc_stack *stack, uint32_t root,
	    struct rc_layer *layer)
{
  int i = find_place (stack, root);
  int width, height;
  if ((i >= 0 && (stack->places[i].parent != 0
		  || !stack->places[i].visible))
      || !stack->size (stack->data, root, &width, &height))
    return false;
  *layer = (struct rc_layer) { .frame = root,
			       .clip = { 0, 0, width, height } };
  return true;
}

/* Put the layer of the frame of PLACE in LAYER, from PARENT, the layer
   of its parent, and return true.  Return false if the frame is not
   visible or has no size.  */
static bool
child_layer (const struct rc_stack *stack,
	     const struct rd_place_frame *place,
	     const struct rc_layer *parent, struct rc_layer *layer)
{
  int width, height;
  if (!place->visible
      || !stack->size (stack->data, place->frame, &width, &height))
    return false;
  layer->frame = place->frame;
  layer->x = parent->x + place->x;
  layer->y = parent->y + place->y;
  layer->clip = intersect (layer->x, layer->y, width, height,
			   &parent->clip);
  return true;
}

/* Add the children of the frame of PARENT, the highest first, to the
   COUNT entries of VISITS.  Return the new number of entries.  */
static int
push_children (const struct rc_stack *stack, const struct rc_layer *parent,
	       struct visit *visits, int count)
{
  int first = count;
  for (int i = 0; i < stack->count; i++)
    if (stack->places[i].parent == parent->frame)
      visits[count++] = (struct visit) { &stack->places[i], *parent };
  qsort (visits + first, count - first, sizeof *visits, compare_visits);
  return count;
}

/* The order of qsort for push_children: the higher sibling comes
   first.  Of two siblings with equal Z, the larger frame number is
   higher.  */
static int
compare_visits (const void *a, const void *b)
{
  const struct rd_place_frame *pa = ((const struct visit *) a)->place;
  const struct rd_place_frame *pb = ((const struct visit *) b)->place;
  if (pa->z != pb->z)
    return pa->z < pb->z ? 1 : -1;
  return pa->frame < pb->frame ? 1 : pa->frame > pb->frame ? -1 : 0;
}

/* Return the part of the rectangle X, Y, WIDTH, HEIGHT that is in
   CLIP.  An empty part is all 0.  The part is in CLIP, so its fields
   fit in an int.  */
static struct rc_rect
intersect (int64_t x, int64_t y, int64_t width, int64_t height,
	   const struct rc_rect *clip)
{
  int64_t left = x > clip->x ? x : clip->x;
  int64_t top = y > clip->y ? y : clip->y;
  int64_t clip_right = (int64_t) clip->x + clip->width;
  int64_t clip_bottom = (int64_t) clip->y + clip->height;
  int64_t right = x + width < clip_right ? x + width : clip_right;
  int64_t bottom = y + height < clip_bottom ? y + height : clip_bottom;
  if (left >= right || top >= bottom)
    return (struct rc_rect) { 0, 0, 0, 0 };
  return (struct rc_rect) { left, top, right - left, bottom - top };
}

/* The function of rc_stack_each_layer for rc_stack_frame_at: keep
   LAYER in the struct search DATA if its clip contains the point.  */
static void
note_layer (const struct rc_layer *layer, void *data)
{
  struct search *search = data;
  if (rect_contains_p (&layer->clip, search->x, search->y))
    {
      search->found = true;
      search->layer = *layer;
    }
}

/* Return true if RECT contains the point X, Y.  */
static bool
rect_contains_p (const struct rc_rect *rect, int x, int y)
{
  return (x >= rect->x && x - rect->x < rect->width
	  && y >= rect->y && y - rect->y < rect->height);
}

/* Return realloc (POINTER, SIZE).  Stop the program if no memory is
   available.  */
static void *
xrealloc (void *pointer, size_t size)
{
  void *result = realloc (pointer, size);
  if (!result)
    {
      fputs ("remote-client: no memory is available\n", stderr);
      abort ();
    }
  return result;
}
