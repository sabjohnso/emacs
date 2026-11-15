/* Test the arguments of SSH for remote-client.

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

/* The laws of the arguments, for a random host and listen spec:

   - Shape: the first argument is the SSH program, a null pointer ends
     the list, and the count is the number of arguments.
   - Host: "--" comes just before the host, so a host that starts with
     "-" is not an option of SSH.  Only the remote command of the
     bootstrap comes after the host.
   - Listen: the remote command with a spec is the remote command with
     no spec, with " --listen=" and the format of the spec after
     "--relay".  The command fits in RC_SSH_COMMAND_SIZE bytes.  The
     word after "--listen=" parses to the spec, so the relay gets the
     spec of the client.

   The examples:

   - Golden: the arguments of a known host, the remote command with no
     spec, and the remote command with a known spec.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "listenspec.h"
#include "sshargs.h"

enum { TRIALS = 1000 };

/* The remote command with no spec.  */
#define BASE_COMMAND \
  "remote-client --relay \"$(emacsclient --eval " \
  "'(remote-display-start)' | tr -d '\"')\""

/* A random case: a host and a listen spec.  */
struct ssh_case
{
  char host[24];
  struct rc_listen_spec spec;
};

static const char *run_trial (uint64_t);
static bool shape_law (uint64_t *);
static bool host_law (uint64_t *);
static bool listen_law (uint64_t *);
static bool golden_example (void);
static bool same_args (const char *const *, const char *const *);
static struct ssh_case random_case (uint64_t *);
static void random_word (uint64_t *, char *, size_t);
static int argument_count (const char *const *);
static int index_of (const char *const *, const char *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "shape", shape_law },
    { "host", host_law },
    { "listen", listen_law },
  };

int
main (int argc, char **argv)
{
  if (!golden_example ())
    {
      fprintf (stderr, "test-sshargs: the golden example failed\n");
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
		   "test-sshargs: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-sshargs: %d trials passed\n", TRIALS);
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

/* The shape law, for a random case of STATE.  */
static bool
shape_law (uint64_t *state)
{
  struct ssh_case c = random_case (state);
  const char *args[RC_SSH_ARGS_SIZE];
  char command[RC_SSH_COMMAND_SIZE];
  rc_ssh_remote_command (&c.spec, command);
  int count = rc_ssh_bootstrap_args ("ssh-x", c.host, command, args);
  if (count != argument_count (args) || count >= RC_SSH_ARGS_SIZE
      || strcmp (args[0], "ssh-x") != 0)
    return false;
  count = rc_ssh_config_args ("ssh-x", c.host, args);
  return (count == argument_count (args) && count < RC_SSH_ARGS_SIZE
	  && strcmp (args[0], "ssh-x") == 0);
}

/* The host law, for a random case of STATE.  */
static bool
host_law (uint64_t *state)
{
  struct ssh_case c = random_case (state);
  const char *args[RC_SSH_ARGS_SIZE];
  char command[RC_SSH_COMMAND_SIZE];
  rc_ssh_remote_command (random_below (state, 2) ? &c.spec : NULL, command);
  int count = rc_ssh_bootstrap_args ("ssh", c.host, command, args);
  if (strcmp (args[count - 3], "--") != 0
      || strcmp (args[count - 2], c.host) != 0
      || strcmp (args[count - 1], command) != 0
      || index_of (args, "--") != count - 3)
    return false;
  count = rc_ssh_config_args ("ssh", c.host, args);
  return (strcmp (args[count - 2], "--") == 0
	  && strcmp (args[count - 1], c.host) == 0
	  && index_of (args, "--") == count - 2);
}

/* The listen law, for a random case of STATE.  */
static bool
listen_law (uint64_t *state)
{
  struct ssh_case c = random_case (state);
  char command[RC_SSH_COMMAND_SIZE + 1], plain[RC_SSH_COMMAND_SIZE];
  char format[RC_LISTEN_TEXT_SIZE], expected[2 * RC_SSH_COMMAND_SIZE];
  memset (command, 'x', sizeof command);
  rc_ssh_remote_command (&c.spec, command);
  rc_ssh_remote_command (NULL, plain);
  rc_listen_spec_format (&c.spec, format);
  static const char relay[] = "--relay";
  const char *after = strstr (plain, relay);
  if (!after || command[RC_SSH_COMMAND_SIZE] != 'x')
    return false;
  after += sizeof relay - 1;
  snprintf (expected, sizeof expected, "%.*s --listen=%s%s",
	    (int) (after - plain), plain, format, after);
  const char *word = strstr (command, "--listen=");
  char text[RC_SSH_COMMAND_SIZE];
  struct rc_listen_spec parsed;
  if (!word)
    return false;
  word += sizeof "--listen=" - 1;
  snprintf (text, sizeof text, "%.*s", (int) strcspn (word, " "), word);
  return strcmp (command, expected) == 0
    && rc_listen_spec_parse (text, &parsed)
    && parsed.family == c.spec.family && parsed.low == c.spec.low
    && parsed.high == c.spec.high
    && memcmp (&parsed.address, &c.spec.address,
	       c.spec.family == AF_INET ? sizeof parsed.address.v4
	       : sizeof parsed.address.v6) == 0;
}

/* The example of known arguments.  */
static bool
golden_example (void)
{
  static const char *const bootstrap[] =
    { "ssh", "--", "work", BASE_COMMAND, NULL };
  static const char *const config[] = { "ssh", "-G", "--", "work", NULL };
  const char *args[RC_SSH_ARGS_SIZE];
  char command[RC_SSH_COMMAND_SIZE];
  rc_ssh_remote_command (NULL, command);
  if (strcmp (command, BASE_COMMAND) != 0
      || rc_ssh_bootstrap_args ("ssh", "work", command, args) != 4
      || !same_args (args, bootstrap)
      || rc_ssh_config_args ("ssh", "work", args) != 4
      || !same_args (args, config))
    return false;
  struct rc_listen_spec spec = { .family = AF_INET, .low = 60001,
				 .high = 60010 };
  inet_pton (AF_INET, "10.0.0.200", &spec.address.v4);
  rc_ssh_remote_command (&spec, command);
  return strcmp (command,
		 "remote-client --relay --listen=10.0.0.200:60001-60010 "
		 "\"$(emacsclient --eval '(remote-display-start)' "
		 "| tr -d '\"')\"") == 0;
}

/* Return true if the argument lists A and B are equal.  */
static bool
same_args (const char *const *a, const char *const *b)
{
  int i = 0;
  for (; a[i] && b[i]; i++)
    if (strcmp (a[i], b[i]) != 0)
      return false;
  return !a[i] && !b[i];
}

/* Return a random case of STATE.  Some hosts start with "-".  The
   spec has the longest IPv6 address or a random IPv4 address.  */
static struct ssh_case
random_case (uint64_t *state)
{
  struct ssh_case c;
  random_word (state, c.host, 1 + random_below (state, sizeof c.host - 1));
  if (random_below (state, 4) == 0)
    c.host[0] = '-';
  memset (&c.spec, 0, sizeof c.spec);
  if (random_below (state, 2) == 0)
    {
      c.spec.family = AF_INET;
      uint32_t bits = random_next (state);
      memcpy (&c.spec.address.v4, &bits, sizeof bits);
    }
  else
    {
      c.spec.family = AF_INET6;
      inet_pton (AF_INET6, "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255",
		 &c.spec.address.v6);
    }
  c.spec.low = 1 + random_below (state, 65535);
  c.spec.high = c.spec.low + random_below (state, 65536 - c.spec.low);
  return c;
}

/* Put a random word of STATE with LENGTH - 1 letters in WORD.  */
static void
random_word (uint64_t *state, char *word, size_t length)
{
  for (size_t i = 0; i + 1 < length; i++)
    word[i] = "abcdefghijklmnopqrstuvwxyz0123456789.-_"
      [random_below (state, 39)];
  word[length - 1] = 0;
  if (length == 1)
    {
      word[0] = 'h';
      word[1] = 0;
    }
}

/* Return the number of arguments before the null pointer of ARGS.  */
static int
argument_count (const char *const *args)
{
  int count = 0;
  while (args[count])
    count++;
  return count;
}

/* Return the index of the first argument of ARGS that is TEXT, or -1.  */
static int
index_of (const char *const *args, const char *text)
{
  for (int i = 0; args[i]; i++)
    if (strcmp (args[i], text) == 0)
      return i;
  return -1;
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
