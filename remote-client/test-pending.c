/* Test the table of the pending connections of the relay.

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

/* The laws of a struct rc_pending_table.  A trace is a random list of
   adds, removes, and clock steps.  A model is a list of the
   connections with their deadlines.

   - Model: after each step, the table holds the connections of the
     model.  An add to a full table removes a connection with the
     earliest deadline.  Two connections can have the same deadline,
     so the law accepts each of them.
   - Wait: rc_pending_wait gives the time to the earliest deadline, or
     0 if it has passed, or -1 for an empty table.
   - Expire: rc_pending_expired gives a connection only if its deadline
     has passed, and it gives each such connection.
   - Feed: when the bytes of a token come in random parts, some of them
     empty, the space moves past each part, and the result is
     RC_PENDING_MORE until the last part.  Then it is RC_PENDING_GOOD if
     the bytes are the expected token, and RC_PENDING_BAD if not.

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

#include "pending.h"

enum { TRIALS = 1000 };

/* The time that a connection waits for its token in the traces.  */
enum { TOKEN_LIMIT = 5000 };

/* The most steps of a trace.  */
enum { STEP_LIMIT = 40 };

/* The model: the deadline of each connection, or -1 for no
   connection, and the number of bytes of its token that came.  The
   connection N has the descriptor 100 + N.  The clock starts at
   TOKEN_LIMIT or later, so no deadline is negative.  */
struct model
{
  int64_t deadline[256];
  size_t got[256];
  int count;
};

static const char *run_trial (uint64_t);
static bool model_law (uint64_t *);
static bool wait_law (uint64_t *);
static bool expire_law (uint64_t *);
static bool feed_law (uint64_t *);
static bool step (struct rc_pending_table *, struct model *, int64_t *,
		  uint64_t *);
static bool expire (struct rc_pending_table *, struct model *, int64_t);
static bool same_p (const struct rc_pending_table *, const struct model *);
static int64_t model_wait (const struct model *, int64_t);
static int64_t random_deadline (const struct model *, int64_t, uint64_t *);
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
    { "wait", wait_law },
    { "expire", expire_law },
    { "feed", feed_law },
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
	  fprintf (stderr,
		   "test-pending: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-pending: %d trials passed\n", TRIALS);
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
  struct rc_pending_table table;
  struct model model = { .count = 0 };
  int64_t now = TOKEN_LIMIT + random_below (state, 1000);
  rc_pending_init (&table);
  for (int i = random_below (state, STEP_LIMIT); i > 0; i--)
    if (!step (&table, &model, &now, state) || !same_p (&table, &model))
      return false;
  return true;
}

/* The wait law, for a random trace of STATE.  */
static bool
wait_law (uint64_t *state)
{
  struct rc_pending_table table;
  struct model model = { .count = 0 };
  int64_t now = TOKEN_LIMIT + random_below (state, 1000);
  rc_pending_init (&table);
  for (int i = random_below (state, STEP_LIMIT); i > 0; i--)
    if (!step (&table, &model, &now, state)
	|| rc_pending_wait (&table, now) != model_wait (&model, now))
      return false;
  return true;
}

/* The expire law, for a random trace of STATE.  */
static bool
expire_law (uint64_t *state)
{
  struct rc_pending_table table;
  struct model model = { .count = 0 };
  int64_t now = TOKEN_LIMIT + random_below (state, 1000);
  rc_pending_init (&table);
  for (int i = random_below (state, STEP_LIMIT); i > 0; i--)
    if (!step (&table, &model, &now, state))
      return false;
  return expire (&table, &model, now) && same_p (&table, &model);
}

/* Remove each expired connection of TABLE at the time NOW, and the
   same connections of MODEL.  Return false if the table gives a
   connection that has not expired, or keeps one that has.  */
static bool
expire (struct rc_pending_table *table, struct model *model, int64_t now)
{
  for (int fd; (fd = rc_pending_expired (table, now)) >= 0;)
    {
      int n = fd - 100;
      if (n < 0 || n >= model->count || model->deadline[n] < 0
	  || model->deadline[n] > now)
	return false;
      model->deadline[n] = -1;
    }
  for (int n = 0; n < model->count; n++)
    if (model->deadline[n] >= 0 && model->deadline[n] <= now)
      return false;
  return true;
}

/* The feed law, for a random token and random parts of STATE.  */
static bool
feed_law (uint64_t *state)
{
  uint8_t expected[RC_TOKEN_SIZE], sent[RC_TOKEN_SIZE];
  for (int i = 0; i < RC_TOKEN_SIZE; i++)
    expected[i] = sent[i] = random_below (state, 256);
  bool good = random_below (state, 2) == 0;
  if (!good)
    sent[random_below (state, RC_TOKEN_SIZE)] ^= 1 + random_below (state, 255);
  struct rc_pending_table table;
  rc_pending_init (&table);
  rc_pending_add (&table, 100, 0);
  struct rc_pending *slot = rc_pending_find (&table, 100);
  size_t got = 0;
  while (got < RC_TOKEN_SIZE)
    {
      size_t size;
      uint8_t *space = rc_pending_space (slot, &size);
      if (size != RC_TOKEN_SIZE - got)
	return false;
      size_t count = random_below (state, size + 1);
      memcpy (space, sent + got, count);
      got += count;
      enum rc_pending_result result = rc_pending_fill (slot, count, expected);
      size_t after;
      if (rc_pending_space (slot, &after) != space + count
	  || after != size - count)
	return false;
      if (result != (got < RC_TOKEN_SIZE ? RC_PENDING_MORE
		     : good ? RC_PENDING_GOOD : RC_PENDING_BAD))
	return false;
    }
  return true;
}

/* Do one random step of STATE on TABLE, on MODEL, and on the time
   *NOW.  Return false if the table gives a wrong result.  */
static bool
step (struct rc_pending_table *table, struct model *model, int64_t *now,
      uint64_t *state)
{
  switch (random_below (state, 4))
    {
    case 3:
      return expire (table, model, *now);
    case 0:
      if (model->count < 256)
	{
	  int live = 0, oldest = -1;
	  for (int n = 0; n < model->count; n++)
	    if (model->deadline[n] >= 0)
	      {
		live++;
		if (oldest < 0
		    || model->deadline[n] < model->deadline[oldest])
		  oldest = n;
	      }
	  int64_t deadline = random_deadline (model, *now, state);
	  int closed = rc_pending_add (table, 100 + model->count, deadline);
	  if (live == RC_PENDING_SIZE)
	    {
	      int n = closed - 100;
	      if (n < 0 || n >= model->count || model->deadline[n] < 0
		  || model->deadline[n] != model->deadline[oldest])
		return false;
	      model->deadline[n] = -1;
	    }
	  else if (closed != -1)
	    return false;
	  /* A new slot has no bytes of a token.  Then the test puts
	     bytes in it, so a slot that is used again shows old
	     bytes.  */
	  struct rc_pending *slot = rc_pending_find (table,
						     100 + model->count);
	  static const uint8_t zeros[RC_TOKEN_SIZE];
	  if (!slot || slot->got != 0
	      || memcmp (slot->token, zeros, RC_TOKEN_SIZE) != 0)
	    return false;
	  slot->got = random_below (state, RC_TOKEN_SIZE);
	  memset (slot->token, 0xa5, slot->got);
	  model->got[model->count] = slot->got;
	  model->deadline[model->count++] = deadline;
	}
      return true;
    case 1:
      if (model->count > 0)
	{
	  int n = random_below (state, model->count);
	  if (model->deadline[n] >= 0)
	    {
	      struct rc_pending *slot = rc_pending_find (table, 100 + n);
	      if (!slot)
		return false;
	      rc_pending_remove (slot);
	      model->deadline[n] = -1;
	    }
	}
      return true;
    default:
      {
	/* Half of the steps stop at the earliest deadline, so the laws
	   see a deadline that is equal to the time.  */
	int64_t wait = model_wait (model, *now);
	*now += (wait > 0 && random_below (state, 2) == 0
		 ? wait : (int64_t) random_below (state,
						  TOKEN_LIMIT / 2));
	return true;
      }
    }
}

/* Return true if TABLE holds the connections of MODEL, and only
   them.  */
static bool
same_p (const struct rc_pending_table *table, const struct model *model)
{
  int live = 0;
  for (int n = 0; n < model->count; n++)
    if (model->deadline[n] >= 0)
      {
	live++;
	struct rc_pending *slot
	  = rc_pending_find ((struct rc_pending_table *) table, 100 + n);
	if (!slot || slot->deadline != model->deadline[n]
	    || slot->got != model->got[n])
	  return false;
      }
  int used = 0;
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    {
      int fd = rc_pending_fd (table, i);
      if (fd >= 0 && (fd < 100 || fd - 100 >= model->count
		      || model->deadline[fd - 100] < 0))
	return false;
      used += fd >= 0;
    }
  return rc_pending_count (table) == live && used == live;
}

/* Return the wait of MODEL at the time NOW.  */
static int64_t
model_wait (const struct model *model, int64_t now)
{
  int64_t wait = -1;
  for (int n = 0; n < model->count; n++)
    if (model->deadline[n] >= 0)
      {
	int64_t left = model->deadline[n] > now ? model->deadline[n] - now : 0;
	if (wait < 0 || left < wait)
	  wait = left;
      }
  return wait;
}

/* Return a random deadline of STATE for a new connection at the time
   NOW of MODEL.  The deadlines are not in the order of the adds.  Some
   have passed, and some are equal to the deadline of another
   connection.  */
static int64_t
random_deadline (const struct model *model, int64_t now, uint64_t *state)
{
  switch (random_below (state, 4))
    {
    case 0:
      if (model->count > 0)
	{
	  int64_t other = model->deadline[random_below (state, model->count)];
	  if (other >= 0)
	    return other;
	}
      return now;
    case 1:
      return now - (int64_t) random_below (state, TOKEN_LIMIT);
    default:
      return now + random_below (state, TOKEN_LIMIT + 1);
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
