/* The bootstrap of a session through SSH for remote-client.

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

#include <config.h>

#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bootstrap.h"
#include "child.h"
#include "clock.h"
#include "sshargs.h"

static pid_t start_ssh (const char *, const char *, int *);
static bool read_line (int, char *, size_t);

bool
rc_bootstrap (const char *ssh, const char *host, struct rc_relay_line *line)
{
  int out;
  pid_t pid = start_ssh (ssh, host, &out);
  if (pid < 0)
    {
      perror ("remote-client: ssh");
      return false;
    }
  char text[RC_RELAY_LINE_SIZE];
  bool ok = read_line (out, text, sizeof text);
  close (out);
  if (!ok)
    kill (pid, SIGTERM);
  waitpid (pid, NULL, 0);
  if (ok && rc_relay_line_parse (text, line))
    return true;
  fprintf (stderr, "remote-client: %s: the session did not start\n", host);
  return false;
}

/* Start the bootstrap on HOST with the program SSH.  Put the read end
   of its standard output in *OUT, and return its process, or -1.  */
static pid_t
start_ssh (const char *ssh, const char *host, int *out)
{
  const char *args[RC_SSH_ARGS_SIZE];
  rc_ssh_bootstrap_args (ssh, host, args);
  int fds[2];
  if (pipe (fds) != 0)
    return -1;
  pid_t pid = fork ();
  if (pid == 0)
    {
      dup2 (fds[1], STDOUT_FILENO);
      close (fds[0]);
      close (fds[1]);
      rc_child_exec (ssh, args);
    }
  close (fds[1]);
  if (pid < 0)
    {
      close (fds[0]);
      return -1;
    }
  *out = fds[0];
  return pid;
}

/* Read one line from FD into TEXT, with the newline and a null byte.
   Return false if FD ends before a newline, if the line does not fit
   in SIZE bytes, or if it does not come in RC_BOOTSTRAP_LIMIT
   milliseconds.  */
static bool
read_line (int fd, char *text, size_t size)
{
  int64_t end = rc_now_ms () + RC_BOOTSTRAP_LIMIT;
  for (size_t length = 0; length + 1 < size; length++)
    {
      struct pollfd ready = { .fd = fd, .events = POLLIN };
      int64_t left = end - rc_now_ms ();
      if (left <= 0 || poll (&ready, 1, left) != 1
	  || read (fd, text + length, 1) != 1)
	return false;
      if (text[length] == '\n')
	{
	  text[length + 1] = 0;
	  return true;
	}
    }
  return false;
}
