/* The tests of the scale of the remote display client.

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

/* The laws of the scale functions, for each scale K from 1 to
   RD_MAX_SCALE:

   - Clamp: rc_scale_clamp gives a scale from 1 to RD_MAX_SCALE, and
     does not change a scale in that range.

   - Identity: with the scale 1, each function gives its argument,
     for a size of at most INT_MAX.

   - Round trip: a logical size to device pixels and back is the same
     size.  The logical position of the device position P times K is
     P.

   - Cover: the logical size of a device size D gives at least D
     device pixels, and fewer than D + K.  So the window of a frame
     shows all of the frame, with less than one logical pixel more.

   - Floor: the logical position L of a device position P is the
     logical pixel that has P: L times K is at most P, and P is less
     than (L + 1) times K.  A negative P has this law too.

   - Point: the device point of a logical point X is X times K.

   - Limit: a device size is at most INT_MAX, because a resize
     message takes an int.  A device size above INT_MAX becomes
     INT_MAX.

   - Monotonic: each function keeps the order of its arguments, also
     for a device size above INT_MAX.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "scale.h"

enum { TRIALS = 10000 };

static const char *run_trial (uint64_t);
static bool clamp_law (uint64_t *);
static bool identity_law (uint64_t *);
static bool round_trip_law (uint64_t *);
static bool cover_law (uint64_t *);
static bool floor_law (uint64_t *);
static bool point_law (uint64_t *);
static bool limit_law (uint64_t *);
static bool monotonic_law (uint64_t *);
static bool cover_p (uint32_t, uint32_t, int);
static bool same_size_p (struct rc_size, struct rc_size);
static bool not_larger_p (struct rc_size, struct rc_size);
static int random_scale (uint64_t *);
static struct rc_size random_size (uint64_t *);
static uint32_t random_length (uint64_t *);
static int64_t random_position (uint64_t *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "clamp", clamp_law },
    { "identity", identity_law },
    { "round trip", round_trip_law },
    { "cover", cover_law },
    { "floor", floor_law },
    { "point", point_law },
    { "limit", limit_law },
    { "monotonic", monotonic_law },
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
	  fprintf (stderr, "test-scale: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-scale: %d trials passed\n", TRIALS);
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

/* The clamp law, for a random scale of GTK.  */
static bool
clamp_law (uint64_t *state)
{
  int scale = (int) random_below (state, 4 * RD_MAX_SCALE) - RD_MAX_SCALE;
  int clamped = rc_scale_clamp (scale);
  bool in_range = scale >= 1 && scale <= RD_MAX_SCALE;
  return (clamped >= 1 && clamped <= RD_MAX_SCALE
	  && (!in_range || clamped == scale));
}

/* The identity law, for a random size and position.  */
static bool
identity_law (uint64_t *state)
{
  struct rc_size size = { random_length (state) % ((uint32_t) INT_MAX + 1),
			  random_length (state) % ((uint32_t) INT_MAX + 1) };
  int64_t position = random_position (state);
  double point = position / 4.0;
  return (same_size_p (rc_scale_to_device (size, 1), size)
	  && same_size_p (rc_scale_to_logical (size, 1), size)
	  && rc_scale_position (position, 1) == position
	  && rc_scale_point (point, 1) == point);
}

/* The round trip law, for a random logical size whose device size is
   not above INT_MAX, and a random position.  */
static bool
round_trip_law (uint64_t *state)
{
  int scale = random_scale (state);
  struct rc_size size = { random_length (state) % (INT_MAX / RD_MAX_SCALE),
			  random_length (state) % (INT_MAX / RD_MAX_SCALE) };
  int64_t position = random_position (state);
  return (same_size_p (rc_scale_to_logical (rc_scale_to_device (size, scale),
					    scale),
		       size)
	  && rc_scale_position (position * scale, scale) == position);
}

/* The cover law, for a random device size.  */
static bool
cover_law (uint64_t *state)
{
  int scale = random_scale (state);
  struct rc_size device = random_size (state);
  struct rc_size logical = rc_scale_to_logical (device, scale);
  return (cover_p (logical.width, device.width, scale)
	  && cover_p (logical.height, device.height, scale));
}

/* Return true if the LOGICAL length at SCALE gives at least DEVICE
   pixels, and fewer than DEVICE + SCALE.  */
static bool
cover_p (uint32_t logical, uint32_t device, int scale)
{
  uint64_t covered = (uint64_t) logical * scale;
  return covered >= device && covered < (uint64_t) device + scale;
}

/* The floor law, for a random device position.  */
static bool
floor_law (uint64_t *state)
{
  int scale = random_scale (state);
  int64_t position = random_position (state);
  int64_t logical = rc_scale_position (position, scale);
  return logical * scale <= position && position < (logical + 1) * scale;
}

/* The point law, for a random logical point with a fraction.  */
static bool
point_law (uint64_t *state)
{
  int scale = random_scale (state);
  double point = random_position (state) / 8.0;
  return rc_scale_point (point, scale) == point * scale;
}

/* The limit law, for a random logical size.  */
static bool
limit_law (uint64_t *state)
{
  int scale = random_scale (state);
  struct rc_size size = random_size (state);
  struct rc_size device = rc_scale_to_device (size, scale);
  uint64_t width = (uint64_t) size.width * scale;
  uint64_t height = (uint64_t) size.height * scale;
  return (device.width == (width > INT_MAX ? INT_MAX : width)
	  && device.height == (height > INT_MAX ? INT_MAX : height));
}

/* The monotonic law, for two random sizes and two random positions.  */
static bool
monotonic_law (uint64_t *state)
{
  int scale = random_scale (state);
  struct rc_size a = random_size (state), b = random_size (state);
  int64_t p = random_position (state), q = random_position (state);
  if (not_larger_p (b, a))
    {
      struct rc_size c = a;
      a = b;
      b = c;
    }
  if (q < p)
    {
      int64_t r = p;
      p = q;
      q = r;
    }
  return (!not_larger_p (a, b)
	  || (not_larger_p (rc_scale_to_device (a, scale),
			    rc_scale_to_device (b, scale))
	      && not_larger_p (rc_scale_to_logical (a, scale),
			       rc_scale_to_logical (b, scale))))
    && rc_scale_position (p, scale) <= rc_scale_position (q, scale);
}

/* Return true if A and B are the same size.  */
static bool
same_size_p (struct rc_size a, struct rc_size b)
{
  return a.width == b.width && a.height == b.height;
}

/* Return true if the width and the height of A are each at most those
   of B.  */
static bool
not_larger_p (struct rc_size a, struct rc_size b)
{
  return a.width <= b.width && a.height <= b.height;
}

/* Return a random scale from 1 to RD_MAX_SCALE.  */
static int
random_scale (uint64_t *state)
{
  return 1 + random_below (state, RD_MAX_SCALE);
}

/* Return a random size.  */
static struct rc_size
random_size (uint64_t *state)
{
  return (struct rc_size) { random_length (state), random_length (state) };
}

/* Return a random width or height.  Most lengths are small, and some
   are at the edges of the range.  */
static uint32_t
random_length (uint64_t *state)
{
  static const uint32_t edges[] = { 0, 1, 32767, INT_MAX / RD_MAX_SCALE,
				    INT_MAX / 2, INT_MAX, UINT32_MAX / 2,
				    UINT32_MAX - 1, UINT32_MAX };
  if (random_below (state, 8) == 0)
    return edges[random_below (state, sizeof edges / sizeof *edges)];
  return random_below (state, 5000);
}

/* Return a random position of a pixel, negative or not.  */
static int64_t
random_position (uint64_t *state)
{
  return (int64_t) random_below (state, 1 << 20) - (1 << 19);
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
