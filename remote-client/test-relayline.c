/* Test the token and the line of the relay of remote-client.

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

/* The laws of the token and of the line of the relay.

   - Token model: rc_token_equal gives the same result as memcmp, for
     random pairs of tokens and for pairs that differ in one byte.

   - Round trip: rc_relay_line_parse of the text of
     rc_relay_line_format gives the line again.

   - Canonical: when rc_relay_line_parse accepts a text, the format of
     the result is the same text.  The text is a valid text with one to
     four random changes, or random bytes, so most texts are not
     valid.  Thus each line has only one text.

   - Keep: when rc_relay_line_parse refuses a text, it does not change
     the line.

   - Each byte: rc_token_equal finds a change at each position of a
     token.

   The examples:

   - New: two new tokens are not equal, and no new token is all zeros.
   - Golden: the text of a known line is the literal text, also for
     the shortest and the longest port.
   - Reject: the parse refuses the port 0, a port larger than 65535,
     a port with a zero before it, a token with capital letters, a
     short token, a long token, a text with no newline, a text with
     bytes after the newline, another version, a sign before the
     port, a space before the text, a letter after f, a null byte, a
     line with no pin, a short pin, a long pin, and a pin with capital
     letters.

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

#include "pin.h"
#include "relayline.h"
#include "token.h"

enum { TRIALS = 1000 };

static const char *run_trial (uint64_t);
static bool token_model_law (uint64_t *);
static bool round_trip_law (uint64_t *);
static bool canonical_law (uint64_t *);
static bool keep_law (uint64_t *);
static bool each_byte_law (uint64_t *);
static void random_text (uint64_t *, char *);
static void change (uint64_t *, char *);
static bool new_example (void);
static bool reject_example (void);
static bool golden_example (void);
static struct rc_relay_line random_line (uint64_t *);
static void random_bytes (uint64_t *, uint8_t *, size_t);
static bool same_line (const struct rc_relay_line *,
		       const struct rc_relay_line *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "token model", token_model_law },
    { "round trip", round_trip_law },
    { "canonical", canonical_law },
    { "keep", keep_law },
    { "each byte", each_byte_law },
  };

/* An example and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} examples[] =
  {
    { "new", new_example },
    { "reject", reject_example },
    { "golden", golden_example },
  };

int
main (int argc, char **argv)
{
  for (size_t i = 0; i < sizeof examples / sizeof *examples; i++)
    if (!examples[i].check ())
      {
	fprintf (stderr, "test-relayline: the %s example failed\n",
		 examples[i].name);
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
	  fprintf (stderr,
		   "test-relayline: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-relayline: %d trials passed\n", TRIALS);
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

/* The token model law, for a random pair of STATE.  */
static bool
token_model_law (uint64_t *state)
{
  uint8_t a[RC_TOKEN_SIZE], b[RC_TOKEN_SIZE];
  random_bytes (state, a, RC_TOKEN_SIZE);
  if (random_below (state, 2) == 0)
    random_bytes (state, b, RC_TOKEN_SIZE);
  else
    {
      memcpy (b, a, sizeof b);
      if (random_below (state, 2) == 0)
	b[random_below (state, RC_TOKEN_SIZE)]
	  ^= 1 + random_below (state, 255);
    }
  return rc_token_equal (a, b) == (memcmp (a, b, RC_TOKEN_SIZE) == 0);
}

/* The round trip law, for a random line of STATE.  */
static bool
round_trip_law (uint64_t *state)
{
  struct rc_relay_line line = random_line (state), parsed;
  char text[RC_RELAY_LINE_SIZE];
  rc_relay_line_format (&line, text);
  return rc_relay_line_parse (text, &parsed) && same_line (&line, &parsed);
}

/* The canonical law, for a random text of STATE.  */
static bool
canonical_law (uint64_t *state)
{
  char text[2 * RC_RELAY_LINE_SIZE];
  struct rc_relay_line parsed;
  random_text (state, text);
  if (!rc_relay_line_parse (text, &parsed))
    return true;
  char again[RC_RELAY_LINE_SIZE];
  rc_relay_line_format (&parsed, again);
  return strcmp (text, again) == 0;
}

/* The keep law, for a random text of STATE.  */
static bool
keep_law (uint64_t *state)
{
  char text[2 * RC_RELAY_LINE_SIZE];
  random_text (state, text);
  struct rc_relay_line line = random_line (state), copy = line;
  return rc_relay_line_parse (text, &copy) || same_line (&line, &copy);
}

/* The each byte law, for a random token of STATE.  */
static bool
each_byte_law (uint64_t *state)
{
  uint8_t a[RC_TOKEN_SIZE], b[RC_TOKEN_SIZE];
  random_bytes (state, a, RC_TOKEN_SIZE);
  if (!rc_token_equal (a, a))
    return false;
  for (int i = 0; i < RC_TOKEN_SIZE; i++)
    {
      memcpy (b, a, sizeof b);
      b[i] ^= 1 + random_below (state, 255);
      if (rc_token_equal (a, b))
	return false;
    }
  return true;
}

/* Put a random text of STATE in TEXT.  Half of the texts are random
   bytes.  The other half are the text of a random line with one to
   four changes.  TEXT has room for 2 * RC_RELAY_LINE_SIZE bytes.  */
static void
random_text (uint64_t *state, char *text)
{
  if (random_below (state, 2) == 0)
    {
      size_t length = random_below (state, RC_RELAY_LINE_SIZE + 4);
      for (size_t i = 0; i < length; i++)
	text[i] = 1 + random_below (state, 255);
      text[length] = 0;
      return;
    }
  struct rc_relay_line line = random_line (state);
  rc_relay_line_format (&line, text);
  for (int changes = 1 + random_below (state, 4); changes > 0; changes--)
    change (state, text);
}

/* Replace, delete, or insert one random byte of TEXT.  A null byte
   cuts the text.  */
static void
change (uint64_t *state, char *text)
{
  static const char bytes[] = "0123456789abcdefgABCDEF \n\t\r+-x";
  size_t length = strlen (text);
  size_t at = random_below (state, length + 1);
  char byte = random_below (state, 16) == 0
    ? 0 : bytes[random_below (state, sizeof bytes - 1)];
  switch (random_below (state, 3))
    {
    case 0:
      if (at < length)
	text[at] = byte;
      break;
    case 1:
      if (at < length)
	memmove (text + at, text + at + 1, length - at);
      break;
    default:
      memmove (text + at + 1, text + at, length - at + 1);
      text[at] = byte;
      break;
    }
}

/* The example of two new tokens.  */
static bool
new_example (void)
{
  uint8_t a[RC_TOKEN_SIZE], b[RC_TOKEN_SIZE];
  static const uint8_t zeros[RC_TOKEN_SIZE];
  return (rc_token_new (a) && rc_token_new (b) && !rc_token_equal (a, b)
	  && memcmp (a, zeros, RC_TOKEN_SIZE) != 0);
}

/* The example of known texts.  */
static bool
golden_example (void)
{
  static const struct
  {
    uint16_t port;
    const char *text;
  } cases[] =
    {
      { 1, "remote-display-relay 2 1 " },
      { 80, "remote-display-relay 2 80 " },
      { 65535, "remote-display-relay 2 65535 " },
    };
  static const char rest[]
    = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff "
      "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\n";
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
    {
      struct rc_relay_line line = { .port = cases[i].port }, parsed;
      for (int j = 0; j < RC_TOKEN_SIZE; j++)
	line.token[j] = (j % 16) * 0x11;
      for (int j = 0; j < RC_PIN_SIZE; j++)
	line.pin[j] = 0xff - (j % 16) * 0x11;
      char text[RC_RELAY_LINE_SIZE], expected[2 * RC_RELAY_LINE_SIZE];
      rc_relay_line_format (&line, text);
      snprintf (expected, sizeof expected, "%s%s", cases[i].text, rest);
      if (strcmp (text, expected) != 0
	  || !rc_relay_line_parse (expected, &parsed)
	  || !same_line (&line, &parsed))
	return false;
    }
  return true;
}

/* The example of the texts that the parse refuses.  */
static bool
reject_example (void)
{
#define TOKEN \
  "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
#define PIN \
  " ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"
  static const char *const texts[] =
    {
      "remote-display-relay 2 0 " TOKEN PIN "\n",
      "remote-display-relay 2 65536 " TOKEN PIN "\n",
      "remote-display-relay 2 080 " TOKEN PIN "\n",
      "remote-display-relay 2 80 "
      "00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF"
      PIN "\n",
      "remote-display-relay 2 80 00112233" PIN "\n",
      "remote-display-relay 2 80 " TOKEN "00" PIN "\n",
      "remote-display-relay 2 80 " TOKEN PIN,
      "remote-display-relay 2 80 " TOKEN PIN "\nx",
      "remote-display-relay 1 80 " TOKEN PIN "\n",
      "remote-display-relay 1 80 " TOKEN "\n",
      "remote-display-relay 2 +80 " TOKEN PIN "\n",
      " remote-display-relay 2 80 " TOKEN PIN "\n",
      "remote-display-relay 2 80 "
      "g0112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
      PIN "\n",
      "remote-display-relay 2 80 " TOKEN PIN "\0\n",
      "remote-display-relay 2 80 " TOKEN "\n",
      "remote-display-relay 2 80 " TOKEN " ffeeddcc\n",
      "remote-display-relay 2 80 " TOKEN PIN "00\n",
      "remote-display-relay 2 80 " TOKEN
      " FFEEDDCCBBAA99887766554433221100FFEEDDCCBBAA99887766554433221100\n",
    };
  struct rc_relay_line line;
  if (!rc_relay_line_parse ("remote-display-relay 2 80 " TOKEN PIN "\n",
			    &line)
      || line.port != 80 || line.token[1] != 0x11 || line.pin[1] != 0xee)
    return false;
  for (size_t i = 0; i < sizeof texts / sizeof *texts; i++)
    if (rc_relay_line_parse (texts[i], &line))
      return false;
  return true;
#undef PIN
#undef TOKEN
}

/* Return a random line of STATE.  */
static struct rc_relay_line
random_line (uint64_t *state)
{
  static const uint16_t edges[] =
    { 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 65535 };
  struct rc_relay_line line;
  line.port = (random_below (state, 3) == 0
	       ? edges[random_below (state, sizeof edges / sizeof *edges)]
	       : 1 + random_below (state, 65535));
  random_bytes (state, line.token, RC_TOKEN_SIZE);
  random_bytes (state, line.pin, RC_PIN_SIZE);
  return line;
}

/* Put SIZE random bytes of STATE in BYTES.  */
static void
random_bytes (uint64_t *state, uint8_t *bytes, size_t size)
{
  for (size_t i = 0; i < size; i++)
    bytes[i] = random_below (state, 256);
}

/* Return true if the lines A and B are equal.  */
static bool
same_line (const struct rc_relay_line *a, const struct rc_relay_line *b)
{
  return a->port == b->port
    && memcmp (a->token, b->token, RC_TOKEN_SIZE) == 0
    && memcmp (a->pin, b->pin, RC_PIN_SIZE) == 0;
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
