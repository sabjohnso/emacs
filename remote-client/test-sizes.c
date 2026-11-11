/* The tests of the sizes of a window of the remote display client.

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

/* The laws of a struct rc_sizes:

   - Drag: while the user changes the size, and the server answers some
     resize messages in their order, or defines the frame with a size
     of its own:
     - an allocation sends a resize message if and only if the server
       does not know its size;
     - no answer gives its size to the window;
     - each other define-frame message gives its size to the window,
       and the server knows that size after it.
     A simple model of the list of the sizes with no answer gives the
     expected values.  The answer to a define-frame message is the
     oldest sent size that is equal to it.  The client then forgets
     that size and each older one.

   - Order: an answer is used once, and the sizes that the client sent
     before it have no answer after it.

   - Limit: the client keeps only the newest RC_SIZES_LIMIT sizes with
     no answer.

   - Size: rc_size_of changes a negative width or height to 0.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  The widths and the heights come mostly from a small range,
   so that one size often comes again.  */

#include <config.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "sizes.h"

enum { TRIALS = 1000 };

/* The longest drag of a trial, in steps.  A drag can send more sizes
   than the client keeps.  */
enum { DRAG_LIMIT = 3 * RC_SIZES_LIMIT };

/* The model of the sizes of a window: the sizes with no answer, oldest
   first, and the size that the server knows.  */
struct model
{
  struct rc_size pending[DRAG_LIMIT];
  int npending;
  struct rc_size known;
};

static const char *run_trial (uint64_t);
static bool drag_law (uint64_t *);
static bool order_law (uint64_t *);
static bool limit_law (uint64_t *);
static bool size_law (uint64_t *);
static bool model_allocation (struct model *, struct rc_size);
static bool model_definition (struct model *, struct rc_size);
static int model_oldest (const struct model *, struct rc_size);
static bool same_size_p (struct rc_size, struct rc_size);
static struct rc_size random_size (uint64_t *);
static uint32_t random_length (uint64_t *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "drag", drag_law },
    { "order", order_law },
    { "limit", limit_law },
    { "size", size_law },
  };

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 1 : 0;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    {
      const char *failed = run_trial (base + trial);
      if (failed)
	{
	  fprintf (stderr, "test-sizes: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-sizes: %d trials passed\n", TRIALS);
  return 0;
}

/* Run the trial of SEED: one case of each law.  Return the name of the
   first law that fails, or NULL if each law passes.  */
static const char *
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  for (size_t i = 0; i < sizeof laws / sizeof *laws; i++)
    if (!laws[i].check (&state))
      return laws[i].name;
  return NULL;
}

/* The drag law, for a random drag of STATE.  Each step is an
   allocation, the answer to a size with no answer, or a size of the
   server that is not such a size.  */
static bool
drag_law (uint64_t *state)
{
  struct rc_size first = random_size (state);
  struct rc_sizes sizes = rc_sizes_new (first);
  struct model model = { .npending = 0, .known = first };
  for (int step = 0; step < DRAG_LIMIT; step++)
    {
      uint64_t kind = random_below (state, 4);
      if (kind == 0 && model.npending > 0)
	{
	  struct rc_size answer
	    = model.pending[random_below (state, model.npending)];
	  if (rc_sizes_note_definition (&sizes, answer)
	      != model_definition (&model, answer))
	    return false;
	}
      else if (kind == 1)
	{
	  struct rc_size server = random_size (state);
	  if (model_oldest (&model, server) < 0
	      && (rc_sizes_note_definition (&sizes, server)
		  != model_definition (&model, server)))
	    return false;
	}
      else
	{
	  struct rc_size size = random_size (state);
	  if (rc_sizes_note_allocation (&sizes, size)
	      != model_allocation (&model, size))
	    return false;
	}
    }
  return true;
}

/* The order law, for two random different sizes of STATE that the
   client sent: OLDER, then NEWER.  */
static bool
order_law (uint64_t *state)
{
  struct rc_size older = random_size (state);
  struct rc_size newer = random_size (state);
  if (same_size_p (older, newer))
    newer.width ^= 1;
  struct rc_size first = { older.width ^ 2, older.height ^ 2 };
  struct rc_sizes sizes = rc_sizes_new (first);
  return (rc_sizes_note_allocation (&sizes, older)
	  && rc_sizes_note_allocation (&sizes, newer)
	  && !rc_sizes_note_definition (&sizes, newer)
	  && rc_sizes_note_definition (&sizes, older)
	  && rc_sizes_note_definition (&sizes, newer));
}

/* The limit law: after COUNT different sizes with no answer, the size
   of a random index is an answer if and only if it is one of the
   newest RC_SIZES_LIMIT.  COUNT is from 1 to 3 * RC_SIZES_LIMIT, from
   STATE.  */
static bool
limit_law (uint64_t *state)
{
  uint32_t count = 1 + random_below (state, 3 * RC_SIZES_LIMIT);
  uint32_t probe = 1 + random_below (state, count);
  struct rc_sizes sizes = rc_sizes_new ((struct rc_size) { 0, 0 });
  for (uint32_t i = 1; i <= count; i++)
    if (!rc_sizes_note_allocation (&sizes, (struct rc_size) { i, i }))
      return false;
  bool kept = probe + RC_SIZES_LIMIT > count;
  return (rc_sizes_note_definition (&sizes, (struct rc_size) { probe, probe })
	  == !kept);
}

/* The size law, for a random width and height of STATE.  */
static bool
size_law (uint64_t *state)
{
  static const int edges[] = { INT_MIN, -1, 0, 1, INT_MAX };
  int width = (random_below (state, 2)
	       ? edges[random_below (state, sizeof edges / sizeof *edges)]
	       : (int) random_below (state, 4000) - 2000);
  int height = (int) random_below (state, 4000) - 2000;
  struct rc_size size = rc_size_of (width, height);
  return (size.width == (uint32_t) (width < 0 ? 0 : width)
	  && size.height == (uint32_t) (height < 0 ? 0 : height));
}

/* Note the allocation of SIZE in MODEL, and return true if the client
   must send it.  */
static bool
model_allocation (struct model *model, struct rc_size size)
{
  if (same_size_p (size, model->known))
    return false;
  if (model->npending == RC_SIZES_LIMIT)
    {
      for (int i = 1; i < model->npending; i++)
	model->pending[i - 1] = model->pending[i];
      model->npending--;
    }
  model->pending[model->npending++] = size;
  model->known = size;
  return true;
}

/* Note the define-frame message of SIZE in MODEL, and return true if
   the window must take SIZE.  */
static bool
model_definition (struct model *model, struct rc_size size)
{
  int answer = model_oldest (model, size);
  if (answer < 0)
    {
      model->known = size;
      return true;
    }
  int kept = model->npending - answer - 1;
  for (int i = 0; i < kept; i++)
    model->pending[i] = model->pending[answer + 1 + i];
  model->npending = kept;
  return false;
}

/* Return the index of the oldest size of MODEL with no answer that is
   equal to SIZE, or -1.  */
static int
model_oldest (const struct model *model, struct rc_size size)
{
  for (int i = 0; i < model->npending; i++)
    if (same_size_p (model->pending[i], size))
      return i;
  return -1;
}

/* Return true if A and B are the same size.  */
static bool
same_size_p (struct rc_size a, struct rc_size b)
{
  return a.width == b.width && a.height == b.height;
}

/* Return a random size of STATE.  */
static struct rc_size
random_size (uint64_t *state)
{
  return (struct rc_size) { random_length (state), random_length (state) };
}

/* Return a random width or height of STATE.  One in eight is an edge
   value.  The others come from a small range.  */
static uint32_t
random_length (uint64_t *state)
{
  static const uint32_t edges[] = { 0, 1, UINT32_MAX, 32767 };
  if (random_below (state, 8) == 0)
    return edges[random_below (state, sizeof edges / sizeof *edges)];
  return 1 + random_below (state, 4);
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
