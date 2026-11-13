/* Test the liveness of a connection of remote-client.

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

/* The laws of a struct rc_link.  A trace is a random list of steps.
   Each step moves the clock forward by up to one second, can give
   bytes from the server or a change of network, and then ticks.

   - Model: each tick gives the action of a simple model.  The model
     is lost when the time since the last input is at least the
     timeout.  Else it pings after a change of network, or when the
     time since the last input and the last ping is at least the ping
     interval.  The serial of each ping is the number of pings so
     far.  The clock can start near the limits of 32-bit and 64-bit
     numbers.

   - Final: after the first RC_LINK_LOST, each tick gives
     RC_LINK_LOST, also after more input.

   - Silence: when the server stops at the time T, no tick before
     T + timeout gives RC_LINK_LOST, the first tick at T + timeout or
     later gives RC_LINK_LOST, and the client pings at least
     RC_LINK_PINGS_PER_TIMEOUT - 1 times before that tick.  The ticks
     of the silence come at least once in each rc_link_tick_interval.

   - Tick interval: rc_link_tick_interval is from 1 to 1000
     milliseconds, and at most a third of the ping interval when that
     third is at least 1 millisecond.


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

#include "link.h"

enum { TRIALS = 1000 };

/* The most steps of a trace.  */
enum { STEP_LIMIT = 200 };

/* The longest step of the clock, in milliseconds.  The contract of
   rc_link_tick asks for a tick at least each second.  */
enum { STEP_TIME_LIMIT = 1000 };

/* The model of the liveness of a connection.  */
struct model
{
  int64_t timeout;
  int64_t last_input;
  int64_t last_ping;
  bool ping_now;
  bool lost;
  uint32_t pings;
};

static const char *run_trial (uint64_t);
static bool model_law (uint64_t *);
static bool final_law (uint64_t *);
static bool silence_law (uint64_t *);
static bool tick_interval_law (uint64_t *);
static enum rc_link_action model_tick (struct model *, int64_t);
static int64_t ping_interval (int64_t);
static int64_t random_timeout (uint64_t *);
static int64_t random_start (uint64_t *);
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
    { "final", final_law },
    { "silence", silence_law },
    { "tick interval", tick_interval_law },
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
	  fprintf (stderr, "test-link: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-link: %d trials passed\n", TRIALS);
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
  int64_t now = random_start (state);
  int64_t timeout = random_timeout (state);
  struct rc_link link = rc_link_new (now, timeout);
  struct model model = { .timeout = timeout, .last_input = now,
			 .last_ping = now };
  for (int step = random_below (state, STEP_LIMIT); step > 0; step--)
    {
      now += random_below (state, STEP_TIME_LIMIT + 1);
      if (random_below (state, 4) == 0)
	{
	  rc_link_note_input (&link, now);
	  if (!model.lost)
	    model.last_input = now;
	}
      if (random_below (state, 8) == 0)
	{
	  rc_link_note_network_change (&link);
	  model.ping_now = true;
	}
      uint32_t serial;
      enum rc_link_action action = rc_link_tick (&link, now, &serial);
      if (action != model_tick (&model, now)
	  || (action == RC_LINK_PING && serial != model.pings))
	return false;
    }
  return true;
}

/* The final law, for a random trace of STATE that ends with input
   after the connection is lost.  */
static bool
final_law (uint64_t *state)
{
  int64_t now = 0;
  int64_t timeout = random_timeout (state);
  struct rc_link link = rc_link_new (now, timeout);
  uint32_t serial;
  while (rc_link_tick (&link, now, &serial) != RC_LINK_LOST)
    now += random_below (state, STEP_TIME_LIMIT + 1);
  for (int step = random_below (state, 20); step > 0; step--)
    {
      now += random_below (state, STEP_TIME_LIMIT + 1);
      if (random_below (state, 2) == 0)
	rc_link_note_input (&link, now);
      if (random_below (state, 4) == 0)
	rc_link_note_network_change (&link);
      if (rc_link_tick (&link, now, &serial) != RC_LINK_LOST)
	return false;
    }
  return true;
}

/* The silence law, for a random time of STATE when the server
   stops.  Before that time, the server sends bytes at random.  */
static bool
silence_law (uint64_t *state)
{
  int64_t timeout = random_timeout (state);
  int64_t now = 0;
  struct rc_link link = rc_link_new (now, timeout);
  uint32_t serial;
  /* Before the silence, input comes more often than the timeout, so
     the connection stays live.  */
  int64_t gap_limit = timeout - 1 < STEP_TIME_LIMIT
		      ? timeout - 1 : STEP_TIME_LIMIT;
  for (int step = random_below (state, 50); step > 0; step--)
    {
      now += random_below (state, gap_limit + 1);
      rc_link_note_input (&link, now);
      if (rc_link_tick (&link, now, &serial) == RC_LINK_LOST)
	return false;
    }
  int64_t stop = now;
  int64_t tick_limit = rc_link_tick_interval (timeout);
  int pings = 0;
  for (;;)
    {
      now += random_below (state, tick_limit + 1);
      enum rc_link_action action = rc_link_tick (&link, now, &serial);
      if (action == RC_LINK_LOST)
	return (now >= stop + timeout
		&& pings >= RC_LINK_PINGS_PER_TIMEOUT - 1);
      if (now >= stop + timeout)
	return false;
      pings += action == RC_LINK_PING;
    }
}

/* The tick interval law, for a random timeout of STATE.  */
static bool
tick_interval_law (uint64_t *state)
{
  int64_t timeout = random_timeout (state);
  int64_t tick = rc_link_tick_interval (timeout);
  int64_t third = ping_interval (timeout) / 3;
  return (1 <= tick && tick <= 1000
	  && (third < 1 || tick <= third));
}

/* Return the action of MODEL at the time NOW, and update MODEL.  */
static enum rc_link_action
model_tick (struct model *model, int64_t now)
{
  if (model->lost || now - model->last_input >= model->timeout)
    {
      model->lost = true;
      return RC_LINK_LOST;
    }
  int64_t last = (model->last_input > model->last_ping
		  ? model->last_input : model->last_ping);
  if (model->ping_now || now - last >= ping_interval (model->timeout))
    {
      model->ping_now = false;
      model->last_ping = now;
      model->pings++;
      return RC_LINK_PING;
    }
  return RC_LINK_WAIT;
}

/* Return the ping interval of TIMEOUT.  */
static int64_t
ping_interval (int64_t timeout)
{
  return timeout / RC_LINK_PINGS_PER_TIMEOUT;
}

/* Return a random timeout of STATE.  One in four is an edge value.
   The others are from the smallest timeout to one minute.  */
static int64_t
random_timeout (uint64_t *state)
{
  static const int64_t edges[] =
    { RC_LINK_PINGS_PER_TIMEOUT, RC_LINK_DEFAULT_TIMEOUT, STEP_TIME_LIMIT };
  if (random_below (state, 4) == 0)
    return edges[random_below (state, sizeof edges / sizeof *edges)];
  return RC_LINK_PINGS_PER_TIMEOUT + random_below (state, 60000);
}

/* Return a random start time of STATE.  One in four is near the limit
   of a 32-bit or a 64-bit number, as a monotonic clock can give after
   a long time.  */
static int64_t
random_start (uint64_t *state)
{
  static const int64_t edges[] =
    { INT32_MAX - 1000, (int64_t) UINT32_MAX - 1000, INT64_MAX / 2 };
  int64_t start = random_below (state, 1000000);
  if (random_below (state, 4) == 0)
    start += edges[random_below (state, sizeof edges / sizeof *edges)];
  return start;
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
