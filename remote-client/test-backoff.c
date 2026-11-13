/* Test the schedule of the reconnects of remote-client.

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

/* The laws of a struct rc_backoff.  A trace is a random list of
   failed tries and resets.

   - Model: each wait is RC_BACKOFF_FIRST times 2 to the power of the
     number of failed tries since the last reset, but at most
     RC_BACKOFF_LIMIT.

   - Bounds: each wait is from RC_BACKOFF_FIRST to RC_BACKOFF_LIMIT.

   - Growth: with no reset, no wait is shorter than the wait before
     it, and the waits reach RC_BACKOFF_LIMIT.

   - Example: the first waits are 250, 500, 1000, 2000, 4000, 8000,
     16000, 30000, and 30000 milliseconds.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "backoff.h"

enum { TRIALS = 1000 };

/* The most steps of a trace.  It is more than the number of doublings
   from RC_BACKOFF_FIRST to RC_BACKOFF_LIMIT.  */
enum { STEP_LIMIT = 40 };

static const char *run_trial (uint64_t);
static bool model_law (uint64_t *);
static bool bounds_law (uint64_t *);
static bool growth_law (uint64_t *);
static int64_t model_wait (int);
static bool sequence_example (void);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "model", model_law },
    { "bounds", bounds_law },
    { "growth", growth_law },
  };

int
main (int argc, char **argv)
{
  if (!sequence_example ())
    {
      fprintf (stderr, "test-backoff: the example failed\n");
      return 1;
    }
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 1 : 0;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    {
      const char *failed = run_trial (base + trial);
      if (failed)
	{
	  fprintf (stderr, "test-backoff: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-backoff: %d trials passed\n", TRIALS);
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

/* The model law, for a random trace of STATE.  */
static bool
model_law (uint64_t *state)
{
  struct rc_backoff backoff = rc_backoff_new ();
  int failures = 0;
  for (int step = random_below (state, 4 * STEP_LIMIT); step > 0; step--)
    if (random_below (state, 8) == 0)
      {
	rc_backoff_reset (&backoff);
	failures = 0;
      }
    else if (rc_backoff_next (&backoff) != model_wait (failures++))
      return false;
  return true;
}

/* The bounds law, for a random trace of STATE.  */
static bool
bounds_law (uint64_t *state)
{
  struct rc_backoff backoff = rc_backoff_new ();
  for (int step = random_below (state, 4 * STEP_LIMIT); step > 0; step--)
    if (random_below (state, 8) == 0)
      rc_backoff_reset (&backoff);
    else
      {
	int64_t wait = rc_backoff_next (&backoff);
	if (wait < RC_BACKOFF_FIRST || wait > RC_BACKOFF_LIMIT)
	  return false;
      }
  return true;
}

/* The growth law, after a random number of steps of STATE.  */
static bool
growth_law (uint64_t *state)
{
  struct rc_backoff backoff = rc_backoff_new ();
  for (int step = random_below (state, STEP_LIMIT); step > 0; step--)
    rc_backoff_next (&backoff);
  int64_t last = 0;
  for (int step = 0; step < STEP_LIMIT; step++)
    {
      int64_t wait = rc_backoff_next (&backoff);
      if (wait < last)
	return false;
      last = wait;
    }
  return last == RC_BACKOFF_LIMIT;
}

/* The example of the first waits.  */
static bool
sequence_example (void)
{
  static const int64_t waits[] =
    { 250, 500, 1000, 2000, 4000, 8000, 16000, 30000, 30000 };
  struct rc_backoff backoff = rc_backoff_new ();
  for (size_t i = 0; i < sizeof waits / sizeof *waits; i++)
    if (rc_backoff_next (&backoff) != waits[i])
      return false;
  return true;
}

/* Return the wait after FAILURES failed tries since the last reset.  */
static int64_t
model_wait (int failures)
{
  int64_t wait = RC_BACKOFF_FIRST;
  while (failures-- > 0 && wait < RC_BACKOFF_LIMIT)
    wait *= 2;
  return wait < RC_BACKOFF_LIMIT ? wait : RC_BACKOFF_LIMIT;
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
