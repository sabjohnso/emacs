/* Test the configuration that "ssh -G" gives, for remote-client.

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

/* The laws of rc_ssh_config_hostname, for a random output of
   "ssh -G": random lines of other keys, with zero, one, or two
   "hostname" lines at random places.

   - Model: when the output has a "hostname" line, the result is the
     value of the first one, if it fits.  Otherwise, the function
     returns false, and does not change the name.  The other keys
     include keys that start or end like "hostname".
   - Fit: the value fits if and only if its length is less than the
     size of the buffer.  The sizes are often near the boundary.

   The examples:

   - Golden: the start of a real output gives its host name, also when
     the last line has no newline.
   - Reject: the function refuses an empty value, a value with a
     space, a tab, or a carriage return, a key with a capital letter, a
     key with a space before it, and an empty text.  When the first
     "hostname" line is not valid, it refuses also a valid line after
     it.

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

#include "sshconfig.h"

enum { TRIALS = 1000 };

/* The longest random value.  */
enum { VALUE_SIZE = 40 };

/* A random output: its text, and the value of its first "hostname"
   line, or an empty string if it has none.  */
struct output
{
  char text[4096];
  char first[VALUE_SIZE];
};

static const char *run_trial (uint64_t);
static bool model_law (uint64_t *);
static bool fit_law (uint64_t *);
static bool golden_example (void);
static bool reject_example (void);
static struct output random_output (uint64_t *);
static void random_value (uint64_t *, char *);
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
    { "fit", fit_law },
  };

int
main (int argc, char **argv)
{
  if (!golden_example () || !reject_example ())
    {
      fprintf (stderr, "test-sshconfig: an example failed\n");
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
		   "test-sshconfig: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-sshconfig: %d trials passed\n", TRIALS);
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

/* The model law, for a random output of STATE.  */
static bool
model_law (uint64_t *state)
{
  struct output output = random_output (state);
  char name[VALUE_SIZE] = "unchanged";
  bool found = rc_ssh_config_hostname (output.text, name, sizeof name);
  if (output.first[0] == 0)
    return !found && strcmp (name, "unchanged") == 0;
  return found && strcmp (name, output.first) == 0;
}

/* The fit law, for a random output of STATE with one "hostname" line
   and a random buffer size.  */
static bool
fit_law (uint64_t *state)
{
  char value[VALUE_SIZE];
  random_value (state, value);
  size_t length = strlen (value);
  size_t size = length + random_below (state, 5);
  size = size < 2 ? 0 : size - 2;
  char text[2 * VALUE_SIZE], name[VALUE_SIZE + 4];
  snprintf (text, sizeof text, "user u\nhostname %s\nport 22\n", value);
  memset (name, 'x', sizeof name);
  bool found = rc_ssh_config_hostname (text, name, size);
  if (length >= size)
    return !found && name[0] == 'x';
  return found && strcmp (name, value) == 0 && name[size] == 'x';
}

/* The example of known outputs.  */
static bool
golden_example (void)
{
  char name[RC_SSH_HOSTNAME_SIZE];
  return rc_ssh_config_hostname ("user sbj\nhostname 10.0.0.200\nport 22\n"
				 "addressfamily any\n", name, sizeof name)
    && strcmp (name, "10.0.0.200") == 0
    && rc_ssh_config_hostname ("user sbj\nhostname work.example",
			       name, sizeof name)
    && strcmp (name, "work.example") == 0
    && rc_ssh_config_hostname ("hostname fe80::1\n", name, sizeof name)
    && strcmp (name, "fe80::1") == 0;
}

/* The example of the outputs that the function refuses.  */
static bool
reject_example (void)
{
  static const char *const texts[] =
    {
      "user u\nhostname \nport 22\n", "hostname\n", "hostname a b\n",
      "Hostname a\n", " hostname a\n", "", "hostname a\tb\n",
      "hostname a\r\n", "hostname a b\nhostname c\n",
      "hostname \nhostname c\n",
    };
  char name[RC_SSH_HOSTNAME_SIZE];
  for (size_t i = 0; i < sizeof texts / sizeof *texts; i++)
    if (rc_ssh_config_hostname (texts[i], name, sizeof name))
      return false;
  return true;
}

/* Return a random output of STATE.  */
static struct output
random_output (uint64_t *state)
{
  static const char *const keys[] =
    { "user", "port", "hostnamex", "xhostname", "hostkeyalias",
      "addressfamily", "host", "hostnam" };
  struct output output = { .first = "" };
  size_t used = 0;
  int lines = random_below (state, 12);
  int hosts = random_below (state, 3);
  for (int line = 0; line < lines + hosts; line++)
    {
      char value[VALUE_SIZE];
      random_value (state, value);
      bool host = hosts > 0
	&& random_below (state, lines + hosts - line) < (uint64_t) hosts;
      const char *key = host ? "hostname"
	: keys[random_below (state, sizeof keys / sizeof *keys)];
      if (host)
	{
	  hosts--;
	  if (output.first[0] == 0)
	    strcpy (output.first, value);
	}
      used += snprintf (output.text + used, sizeof output.text - used,
			"%s %s\n", key, value);
    }
  return output;
}

/* Put a random value of STATE with 1 to VALUE_SIZE - 1 characters in
   VALUE.  It has no space and no newline.  */
static void
random_value (uint64_t *state, char *value)
{
  size_t length = 1 + random_below (state, VALUE_SIZE - 1);
  for (size_t i = 0; i < length; i++)
    value[i] = "abcdefghijklmnopqrstuvwxyz0123456789.-:_,@"
      [random_below (state, 42)];
  value[length] = 0;
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
