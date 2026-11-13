/* Tests of the table module of the remote display client.

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

/* The test is a property with a model.  A trial applies random sets
   and removes to a table and to a model, an array with one value for
   each ID of the trial.  After each operation, rc_table_get of each ID
   gives the value of the model.  So these laws hold:

   - get after set gives the value of the set, also for an ID that
     the table already has.
   - remove gives the value of the ID, or NULL for an ID that the table
     does not have, and get then gives NULL.
   - set and remove of one ID do not change the value of another ID.
   - rc_table_free calls its function once for each value.
   - rc_table_count gives the number of IDs that have a value.
   - rc_table_each calls its function once for each ID that has a
     value, with that value.

   With no argument, the program runs TRIALS trials from a seed that
   it gets from the clock and the process ID.  With the argument SEED,
   it runs only the trial of that seed.  */

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "table.h"

enum { TRIALS = 300, STEPS = 200 };

/* The IDs of a trial.  The table grows above its first size.  */
static const uint32_t ids[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
				UINT32_MAX - 1, UINT32_MAX };
enum { NIDS = sizeof ids / sizeof *ids };

/* The values of a trial: the addresses of the elements of VALUES.  */
static char values[16];

static bool run_trial (uint64_t);
static void count_value (void *);
static bool each_matches_p (const struct rc_table *, void *const *);
static void visit_entry (uint32_t, void *, void *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* The number of calls of count_value.  */
static int freed;

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 0 : 1;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    if (!run_trial (base + trial))
      {
	fprintf (stderr, "test-table: seed %llu failed\n",
		 (unsigned long long) (base + trial));
	return 1;
      }
  printf ("test-table: %d trials passed\n", TRIALS);
  return 0;
}

/* Run the trial of SEED.  Return true if it passes.  */
static bool
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  struct rc_table table = { 0 };
  void *model[NIDS] = { 0 };
  bool passed = true;
  for (int step = 0; passed && step < STEPS; step++)
    {
      int i = random_below (&state, NIDS);
      if (random_below (&state, 2))
	{
	  void *value = &values[random_below (&state, sizeof values)];
	  rc_table_set (&table, ids[i], value);
	  model[i] = value;
	}
      else
	{
	  passed = rc_table_remove (&table, ids[i]) == model[i];
	  model[i] = NULL;
	}
      for (int j = 0; passed && j < NIDS; j++)
	passed = rc_table_get (&table, ids[j]) == model[j];
      passed = passed && each_matches_p (&table, model);
    }
  int present = 0;
  for (int j = 0; j < NIDS; j++)
    present += model[j] != NULL;
  passed = passed && rc_table_count (&table) == present;
  freed = 0;
  rc_table_free (&table, count_value);
  return passed && freed == present;
}

/* The visits of rc_table_each: the number of visits of each ID, and
   true if each visit had the value of the model.  */
struct visits
{
  void *const *model;
  int count[NIDS];
  bool same;
};

/* Return true if rc_table_each of TABLE visits each ID of MODEL that
   has a value once, with that value, and no other ID, and if
   rc_table_count of TABLE is the number of these IDs.  */
static bool
each_matches_p (const struct rc_table *table, void *const *model)
{
  struct visits visits = { .model = model, .same = true };
  rc_table_each (table, visit_entry, &visits);
  int present = 0;
  for (int j = 0; j < NIDS; j++)
    {
      present += model[j] != NULL;
      if (visits.count[j] != (model[j] != NULL))
	return false;
    }
  return visits.same && rc_table_count (table) == present;
}

/* The function of rc_table_each.  DATA is the struct visits.  Record
   the visit of ID with VALUE.  */
static void
visit_entry (uint32_t id, void *value, void *data)
{
  struct visits *visits = data;
  for (int j = 0; j < NIDS; j++)
    if (ids[j] == id)
      {
	visits->count[j]++;
	visits->same = visits->same && visits->model[j] == value;
	return;
      }
  visits->same = false;
}

/* The function of rc_table_free: count the calls.  */
static void
count_value (void *value)
{
  (void) value;
  freed++;
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
