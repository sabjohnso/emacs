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
#include "progname.h"
#include "sshargs.h"

static bool read_ssh (const char *, const char *const *, bool, char *,
		      size_t);
static pid_t start_ssh (const char *, const char *const *, int *);
static bool read_output (int, bool, char *, size_t);

bool
rc_bootstrap (const char *ssh, const char *host,
	      const struct rc_listen_spec *spec, const char *session,
	      struct rc_relay_line *line)
{
  char command[RC_SSH_COMMAND_SIZE];
  const char *args[RC_SSH_ARGS_SIZE];
  if (!rc_ssh_remote_command (spec, session, command))
    {
      fprintf (stderr, RC_PROGRAM ": %s: not a name of a session\n",
	       session);
      return false;
    }
  rc_ssh_bootstrap_args (ssh, host, command, args);
  char text[RC_RELAY_LINE_SIZE];
  if (read_ssh (ssh, args, true, text, sizeof text)
      && rc_relay_line_parse (text, line))
    return true;
  fprintf (stderr, RC_PROGRAM ": %s: the session did not start\n", host);
  return false;
}

bool
rc_bootstrap_hostname (const char *ssh, const char *host,
		       char name[RC_SSH_HOSTNAME_SIZE])
{
  const char *args[RC_SSH_ARGS_SIZE];
  rc_ssh_config_args (ssh, host, args);
  char text[RC_SSH_CONFIG_SIZE];
  if (read_ssh (ssh, args, false, text, sizeof text)
      && rc_ssh_config_hostname (text, name, RC_SSH_HOSTNAME_SIZE))
    return true;
  fprintf (stderr, RC_PROGRAM ": %s: ssh -G gave no host name\n", host);
  return false;
}

/* Run the program SSH with ARGS, and read its standard output into
   TEXT, a buffer of SIZE bytes, with a null byte after it.  With LINE,
   read only the first line, with its newline.  Otherwise, read to the
   end.  Return false if SSH does not start, or if read_output
   fails.  */
static bool
read_ssh (const char *ssh, const char *const *args, bool line, char *text,
	  size_t size)
{
  int out;
  pid_t pid = start_ssh (ssh, args, &out);
  if (pid < 0)
    {
      perror (RC_PROGRAM ": ssh");
      return false;
    }
  bool ok = read_output (out, line, text, size);
  close (out);
  if (!ok)
    kill (pid, SIGTERM);
  waitpid (pid, NULL, 0);
  return ok;
}

/* Start the program SSH with ARGS.  Put the read end of its standard
   output in *OUT, and return its process, or -1.  */
static pid_t
start_ssh (const char *ssh, const char *const *args, int *out)
{
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

/* Read from FD into TEXT, a buffer of SIZE bytes, and put a null byte
   after the bytes.  With LINE, stop after the first newline, and
   return false if FD ends before it.  Otherwise, stop at the end of
   FD.  Return false if the bytes do not fit in SIZE bytes, or if they
   do not come in RC_BOOTSTRAP_LIMIT milliseconds.  */
static bool
read_output (int fd, bool line, char *text, size_t size)
{
  int64_t end = rc_now_ms () + RC_BOOTSTRAP_LIMIT;
  size_t length = 0;
  while (length + 1 < size)
    {
      struct pollfd ready = { .fd = fd, .events = POLLIN };
      int64_t left = end - rc_now_ms ();
      if (left <= 0 || poll (&ready, 1, left) != 1)
	return false;
      /* A line comes one byte at a time, so no byte after it is lost.  */
      ssize_t count = read (fd, text + length,
			    line ? 1 : size - 1 - length);
      if (count < 0)
	return false;
      text[length + count] = 0;
      if (count == 0)
	return !line;
      length += count;
      if (line && text[length - 1] == '\n')
	return true;
    }
  return false;
}
