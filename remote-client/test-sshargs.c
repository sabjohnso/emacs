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

/* The laws of the arguments, for a random host, socket, and port:

   - Shape: the first argument is the SSH program, a null pointer ends
     the list, and the count is the number of arguments.
   - Host: "--" comes just before the host, so a host that starts with
     "-" is not an option of SSH.  Only the remote command of the
     bootstrap comes after the host.
   - Forward: the argument after "-L" is the socket, ":127.0.0.1:", and
     the port in decimal.
   - Fit: the tunnel arguments fail if and only if the forward does
     not fit in its buffer.  The sizes are often near the boundary, and
     a buffer of RC_SSH_FORWARD_SIZE bytes holds the forward of each
     socket name of up to 108 bytes.

   The examples:

   - Golden: the arguments of a known host, socket, and port.

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

#include "sshargs.h"

enum { TRIALS = 1000 };

/* A random case: a host, a socket, a port, and the size of the buffer
   of the forward.  */
struct ssh_case
{
  char host[24];
  char socket[109];
  uint16_t port;
  size_t size;
};

static const char *run_trial (uint64_t);
static bool shape_law (uint64_t *);
static bool host_law (uint64_t *);
static bool forward_law (uint64_t *);
static bool fit_law (uint64_t *);
static bool golden_example (void);
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
    { "forward", forward_law },
    { "fit", fit_law },
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
  char forward[RC_SSH_FORWARD_SIZE];
  int count = rc_ssh_bootstrap_args ("ssh-x", c.host, args);
  if (count != argument_count (args) || count >= RC_SSH_ARGS_SIZE
      || strcmp (args[0], "ssh-x") != 0)
    return false;
  count = rc_ssh_tunnel_args ("ssh-x", c.host, c.socket, c.port, forward,
			      sizeof forward, args);
  return (count == argument_count (args) && count < RC_SSH_ARGS_SIZE
	  && strcmp (args[0], "ssh-x") == 0);
}

/* The host law, for a random case of STATE.  */
static bool
host_law (uint64_t *state)
{
  struct ssh_case c = random_case (state);
  const char *args[RC_SSH_ARGS_SIZE];
  char forward[RC_SSH_FORWARD_SIZE];
  int count = rc_ssh_bootstrap_args ("ssh", c.host, args);
  if (strcmp (args[count - 3], "--") != 0
      || strcmp (args[count - 2], c.host) != 0
      || strcmp (args[count - 1], rc_ssh_remote_command) != 0
      || index_of (args, "--") != count - 3)
    return false;
  count = rc_ssh_tunnel_args ("ssh", c.host, c.socket, c.port, forward,
			      sizeof forward, args);
  return (strcmp (args[count - 2], "--") == 0
	  && strcmp (args[count - 1], c.host) == 0
	  && index_of (args, "--") == count - 2);
}

/* The forward law, for a random case of STATE.  */
static bool
forward_law (uint64_t *state)
{
  struct ssh_case c = random_case (state);
  const char *args[RC_SSH_ARGS_SIZE];
  char forward[RC_SSH_FORWARD_SIZE];
  rc_ssh_tunnel_args ("ssh", c.host, c.socket, c.port, forward,
		      sizeof forward, args);
  char expected[RC_SSH_FORWARD_SIZE];
  snprintf (expected, sizeof expected, "%s:127.0.0.1:%u", c.socket,
	    (unsigned) c.port);
  int l = index_of (args, "-L");
  return l >= 0 && strcmp (args[l + 1], expected) == 0;
}

/* The fit law, for a random case of STATE with a random buffer size.  */
static bool
fit_law (uint64_t *state)
{
  struct ssh_case c = random_case (state);
  const char *args[RC_SSH_ARGS_SIZE];
  char forward[RC_SSH_FORWARD_SIZE];
  char text[RC_SSH_FORWARD_SIZE];
  size_t needed = snprintf (text, sizeof text, "%s:127.0.0.1:%u", c.socket,
			    (unsigned) c.port) + 1;
  if (random_below (state, 2) == 0)
    {
      int64_t near = (int64_t) needed + (int64_t) random_below (state, 5) - 2;
      c.size = near < 0 ? 0 : near;
    }
  int count = rc_ssh_tunnel_args ("ssh", c.host, c.socket, c.port, forward,
				  c.size, args);
  return ((count < 0) == (needed > c.size)
	  && rc_ssh_tunnel_args ("ssh", c.host, c.socket, c.port, forward,
				 sizeof forward, args) > 0);
}

/* The example of known arguments.  */
static bool
golden_example (void)
{
  static const char *const bootstrap[] =
    { "ssh", "--", "work", NULL };
  static const char *const tunnel[] =
    { "ssh", "-N", "-o", "ExitOnForwardFailure=yes",
      "-o", "ServerAliveInterval=5", "-o", "ServerAliveCountMax=2",
      "-L", "/run/s:127.0.0.1:4321", "--", "work", NULL };
  const char *args[RC_SSH_ARGS_SIZE];
  char forward[RC_SSH_FORWARD_SIZE];
  int count = rc_ssh_bootstrap_args ("ssh", "work", args);
  if (count != 4)
    return false;
  for (int i = 0; i < 3; i++)
    if (strcmp (args[i], bootstrap[i]) != 0)
      return false;
  if (args[3] != rc_ssh_remote_command || args[4] != NULL)
    return false;
  if (strcmp (rc_ssh_remote_command,
	      "remote-client --relay \"$(emacsclient --eval "
	      "'(remote-display-start)' | tr -d '\"')\"") != 0)
    return false;
  count = rc_ssh_tunnel_args ("ssh", "work", "/run/s", 4321, forward,
			      sizeof forward, args);
  if (count != (int) (sizeof tunnel / sizeof *tunnel) - 1)
    return false;
  for (int i = 0; i <= count; i++)
    if ((args[i] == NULL) != (tunnel[i] == NULL)
	|| (args[i] && strcmp (args[i], tunnel[i]) != 0))
      return false;
  return true;
}

/* Return a random case of STATE.  Some hosts start with "-".  */
static struct ssh_case
random_case (uint64_t *state)
{
  struct ssh_case c;
  random_word (state, c.host, 1 + random_below (state, sizeof c.host - 1));
  if (random_below (state, 4) == 0)
    c.host[0] = '-';
  random_word (state, c.socket,
	       random_below (state, 4) == 0
	       ? sizeof c.socket
	       : 1 + random_below (state, sizeof c.socket - 1));
  c.socket[0] = '/';
  c.port = 1 + random_below (state, 65535);
  c.size = random_below (state, RC_SSH_FORWARD_SIZE + 1);
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
