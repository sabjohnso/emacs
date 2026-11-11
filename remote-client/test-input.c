/* Tests of the input module of the remote display client.

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

/* The laws of the input messages:

   - key fields, key modifiers: see below.
   - button: the message of a button press or release has the frame,
     the time, the button, the press, and the modifiers of the event,
     and its position rounded down to a pixel.
   - motion: the message of a motion has the frame, the time, and the
     position rounded down.
   - scroll: a scroll up, down, left, or right gives the wheel button
     4, 5, 6, or 7.  A smooth scroll gives the button of its larger
     delta, the vertical one for equal deltas, and a scroll with no
     delta gives 0.

   The laws of the key message of a key press:

   - fields: the message has the frame, the time, and the key value of
     the press.  Its character is the Unicode character of the key
     value, or 0.
   - modifiers: each GDK modifier of the press gives its bit, and no
     other GDK state gives a bit.  Shift gives shift, Control gives
     control, Mod1 and Meta give meta, Super gives super, and Hyper
     gives hyper, as in pgtkterm.c with its default settings.

   The test needs no display.  With no argument, the program runs the
   examples and TRIALS trials from a seed that it gets from the clock
   and the process ID.  With the argument SEED, it runs only the trial
   of that seed.  */

#include <config.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <gdk/gdk.h>

#include "input.h"
#include "rdproto.h"

enum { TRIALS = 1000 };

/* The GDK modifiers and their bits.  */
static const struct
{
  GdkModifierType mask;
  uint8_t bit;
} modifiers[] =
  {
    { GDK_SHIFT_MASK, RD_SHIFT },
    { GDK_CONTROL_MASK, RD_CONTROL },
    { GDK_MOD1_MASK, RD_META },
    { GDK_META_MASK, RD_META },
    { GDK_SUPER_MASK, RD_SUPER },
    { GDK_HYPER_MASK, RD_HYPER },
  };
enum { NMODIFIERS = sizeof modifiers / sizeof *modifiers };

/* Other GDK states, which give no bit.  */
static const GdkModifierType other_states[] =
  {
    GDK_LOCK_MASK, GDK_MOD2_MASK, GDK_MOD3_MASK, GDK_MOD4_MASK,
    GDK_MOD5_MASK, GDK_BUTTON1_MASK, GDK_BUTTON3_MASK, GDK_RELEASE_MASK,
  };
enum { NOTHER = sizeof other_states / sizeof *other_states };

static bool run_examples (void);
static bool example (guint, uint32_t);
static bool run_trial (uint64_t);
static bool scroll_examples (void);
static int32_t rc_message_x_of (double);
static int expected_wheel (double, double);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 0 : 1;
  if (!run_examples () || !scroll_examples ())
    return 1;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    if (!run_trial (base + trial))
      {
	fprintf (stderr, "test-input: seed %llu failed\n",
		 (unsigned long long) (base + trial));
	return 1;
      }
  printf ("test-input: the examples and %d trials passed\n", TRIALS);
  return 0;
}

/* The examples of the character of a key value.  */
static bool
run_examples (void)
{
  bool passed = (example (GDK_KEY_a, 'a') && example (GDK_KEY_A, 'A')
		 && example (GDK_KEY_Return, '\r')
		 && example (GDK_KEY_F1, 0)
		 && example (GDK_KEY_eacute, 0xe9)
		 && example (GDK_KEY_Greek_alpha, 0x3b1));
  if (!passed)
    fprintf (stderr, "test-input: an example of a character failed\n");
  return passed;
}

/* Return true if the key message of KEYVAL has the character
   CHARACTER.  */
static bool
example (guint keyval, uint32_t character)
{
  struct rd_message message = rc_key_message (1, keyval, 0, 0);
  return (message.type == RD_KEY && message.u.key.keysym == keyval
	  && message.u.key.character == character);
}

/* The scroll law.  */
static bool
scroll_examples (void)
{
  bool passed = (rc_scroll_button (GDK_SCROLL_UP, 0, 0) == 4
		 && rc_scroll_button (GDK_SCROLL_DOWN, 0, 0) == 5
		 && rc_scroll_button (GDK_SCROLL_LEFT, 0, 0) == 6
		 && rc_scroll_button (GDK_SCROLL_RIGHT, 0, 0) == 7
		 && rc_scroll_button (GDK_SCROLL_SMOOTH, 0, -1.5) == 4
		 && rc_scroll_button (GDK_SCROLL_SMOOTH, 0.5, 2) == 5
		 && rc_scroll_button (GDK_SCROLL_SMOOTH, -3, 1) == 6
		 && rc_scroll_button (GDK_SCROLL_SMOOTH, 2, -0.5) == 7
		 && rc_scroll_button (GDK_SCROLL_SMOOTH, 0, 0) == 0
		 && rc_scroll_button (GDK_SCROLL_SMOOTH, 1, 1) == 5
		 && rc_scroll_button (GDK_SCROLL_SMOOTH, -1, -1) == 4
		 && rc_scroll_button (GDK_SCROLL_UP, 9, 9) == 4
		 && rc_message_x_of (-0.5) == -1);
  if (!passed)
    fprintf (stderr, "test-input: an example of a scroll failed\n");
  return passed;
}

/* Return the x of the motion message of a motion to X, Y = 0.  */
static int32_t
rc_message_x_of (double x)
{
  return rc_motion_message (1, x, 0, 0).u.motion.x;
}

/* Return the wheel button that the scroll law gives for a smooth
   scroll with the deltas DX and DY.  */
static int
expected_wheel (double dx, double dy)
{
  if (dy != 0 && fabs (dy) >= fabs (dx))
    return dy < 0 ? 4 : 5;
  if (dx != 0)
    return dx < 0 ? 6 : 7;
  return 0;
}

/* Run the trial of SEED: a key press, a button event, a motion, and a
   smooth scroll, with random values.  Return true if it passes.  */
static bool
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  uint32_t frame = random_next (&state);
  uint32_t time = random_next (&state);
  guint keyval = (random_below (&state, 2) ? random_below (&state, 0x10000)
		  : 0x1000000 + random_below (&state, 0x110000));
  GdkModifierType gdk_state = 0;
  uint8_t bits = 0;
  for (int i = 0; i < NMODIFIERS; i++)
    if (random_below (&state, 2))
      {
	gdk_state |= modifiers[i].mask;
	bits |= modifiers[i].bit;
      }
  for (int i = 0; i < NOTHER; i++)
    if (random_below (&state, 2))
      gdk_state |= other_states[i];
  struct rd_message message = rc_key_message (frame, keyval, gdk_state,
					      time);
  const struct rd_key *key = &message.u.key;
  double x = (double) random_below (&state, 200000) / 100 - 1000;
  double y = (double) random_below (&state, 200000) / 100 - 1000;
  guint button = 1 + random_below (&state, 9);
  bool press = random_below (&state, 2);
  struct rd_message click = rc_button_message (frame, button, x, y,
					       gdk_state, time, press);
  const struct rd_button *b = &click.u.button;
  struct rd_message move = rc_motion_message (frame, x, y, time);
  const struct rd_motion *m = &move.u.motion;
  static const double deltas[] = { 0, 0.5, -0.5, 1, -1, 2.5, -2.5 };
  double dx = deltas[random_below (&state, 7)];
  double dy = deltas[random_below (&state, 7)];
  if (rc_scroll_button (GDK_SCROLL_SMOOTH, dx, dy) != expected_wheel (dx, dy))
    return false;
  return (message.type == RD_KEY && key->frame == frame
	  && key->time == time && key->keysym == keyval
	  && key->character == gdk_keyval_to_unicode (keyval)
	  && key->modifiers == bits
	  && click.type == RD_BUTTON && b->frame == frame && b->time == time
	  && b->x == floor (x) && b->y == floor (y) && b->button == button
	  && b->press == press && b->modifiers == bits
	  && move.type == RD_MOTION && m->frame == frame && m->time == time
	  && m->x == floor (x) && m->y == floor (y));
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
