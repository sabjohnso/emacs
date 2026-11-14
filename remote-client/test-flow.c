/* Test the flow of the relay of remote-client.

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

/* The laws of a struct rc_flow.  A trace is a random list of fills
   and drains of random sizes.  A fill puts the next bytes of a random
   input in an empty flow.  A drain takes some of the bytes of the
   flow.

   - Conservation: the bytes that the drains take are the bytes that
     the fills put, in the same order, with none lost and none added.
   - Capacity: a flow never holds more than RC_FLOW_SIZE bytes, and it
     takes bytes only when it is empty.
   - Empty: at each step, a flow is empty if and only if it holds no
     bytes, and then its space has RC_FLOW_SIZE bytes.  After a drain
     of all its bytes, a flow is empty.

   A read or a drain can have no bytes, and a drain often takes all the
   bytes, so the traces often go from a full flow to an empty flow.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "flow.h"

enum { TRIALS = 200 };

/* The most steps of a trace.  */
enum { STEP_LIMIT = 64 };

static const char *run_trial (uint64_t);
static bool conservation_law (uint64_t *);
static bool capacity_law (uint64_t *);
static bool empty_law (uint64_t *);
static size_t fill (struct rc_flow *, uint64_t *, size_t);
static size_t random_size (uint64_t *);
static size_t random_drain (uint64_t *, size_t);
static bool consistent_p (struct rc_flow *);
static uint8_t input_byte (size_t);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "conservation", conservation_law },
    { "capacity", capacity_law },
    { "empty", empty_law },
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
	  fprintf (stderr, "test-flow: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-flow: %d trials passed\n", TRIALS);
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

/* The conservation law, for a random trace of STATE.  */
static bool
conservation_law (uint64_t *state)
{
  static struct rc_flow flow;
  rc_flow_init (&flow);
  size_t put = 0, taken = 0;
  for (int step = random_below (state, STEP_LIMIT); step > 0; step--)
    if (rc_flow_empty_p (&flow))
      put += fill (&flow, state, put);
    else
      {
	size_t count;
	const unsigned char *bytes = rc_flow_bytes (&flow, &count);
	size_t drain = random_drain (state, count);
	for (size_t i = 0; i < drain; i++)
	  if (bytes[i] != input_byte (taken + i))
	    return false;
	rc_flow_drain (&flow, drain);
	taken += drain;
      }
  size_t count = 0;
  if (!rc_flow_empty_p (&flow))
    rc_flow_bytes (&flow, &count);
  return taken + count == put;
}

/* The capacity law, for a random trace of STATE.  */
static bool
capacity_law (uint64_t *state)
{
  static struct rc_flow flow;
  rc_flow_init (&flow);
  size_t put = 0;
  for (int step = random_below (state, STEP_LIMIT); step > 0; step--)
    {
      size_t free_size;
      bool empty = rc_flow_empty_p (&flow);
      unsigned char *space = rc_flow_space (&flow, &free_size);
      if (empty ? (!space || free_size != RC_FLOW_SIZE) : free_size != 0)
	return false;
      if (empty)
	put += fill (&flow, state, put);
      else
	{
	  size_t count;
	  rc_flow_bytes (&flow, &count);
	  if (count > RC_FLOW_SIZE)
	    return false;
	  rc_flow_drain (&flow, random_below (state, count + 1));
	}
    }
  return true;
}

/* The empty law, for a random trace of STATE.  */
static bool
empty_law (uint64_t *state)
{
  static struct rc_flow flow;
  rc_flow_init (&flow);
  if (!rc_flow_empty_p (&flow) || !consistent_p (&flow))
    return false;
  size_t put = 0;
  for (int step = random_below (state, STEP_LIMIT); step > 0; step--)
    {
      if (rc_flow_empty_p (&flow))
	put += fill (&flow, state, put);
      if (!consistent_p (&flow))
	return false;
      size_t count;
      rc_flow_bytes (&flow, &count);
      rc_flow_drain (&flow, random_drain (state, count));
      if (!consistent_p (&flow))
	return false;
    }
  size_t count;
  rc_flow_bytes (&flow, &count);
  rc_flow_drain (&flow, count);
  return rc_flow_empty_p (&flow) && consistent_p (&flow);
}

/* Return true if FLOW is empty if and only if it holds no bytes, and
   its space has RC_FLOW_SIZE bytes if it is empty, and none if not.  */
static bool
consistent_p (struct rc_flow *flow)
{
  size_t count, size;
  rc_flow_bytes (flow, &count);
  rc_flow_space (flow, &size);
  bool empty = rc_flow_empty_p (flow);
  return empty == (count == 0) && size == (empty ? RC_FLOW_SIZE : 0);
}

/* Fill the empty FLOW with a random number of the input bytes after
   the first PUT bytes.  Return the number of bytes.  */
static size_t
fill (struct rc_flow *flow, uint64_t *state, size_t put)
{
  size_t free_size;
  unsigned char *space = rc_flow_space (flow, &free_size);
  size_t count = random_size (state);
  if (count > free_size)
    count = free_size;
  for (size_t i = 0; i < count; i++)
    space[i] = input_byte (put + i);
  rc_flow_fill (flow, count);
  return count;
}

/* Return a random size of a read: sometimes 0, often small, sometimes
   the full buffer.  */
static size_t
random_size (uint64_t *state)
{
  switch (random_below (state, 4))
    {
    case 0:
      return random_below (state, 16);
    case 1:
      return 0;
    case 2:
      return 1 + random_below (state, RC_FLOW_SIZE);
    default:
      return RC_FLOW_SIZE;
    }
}

/* Return a random size of a drain of a flow with COUNT bytes: all the
   bytes, one byte, or a random number of bytes.  */
static size_t
random_drain (uint64_t *state, size_t count)
{
  switch (random_below (state, 3))
    {
    case 0:
      return count;
    case 1:
      return count < 1 ? count : 1;
    default:
      return random_below (state, count + 1);
    }
}

/* Return the byte at POSITION of the input.  */
static uint8_t
input_byte (size_t position)
{
  return position * 131 + (position >> 8);
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
