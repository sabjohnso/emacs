/* The arguments of SSH for remote-client.

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

#include <assert.h>
#include <stdio.h>

#include "sshargs.h"

static int copy_args (const char *const *, const char **);

const char rc_ssh_remote_command[]
  = "remote-client --relay \"$(emacsclient --eval "
    "'(remote-display-start)' | tr -d '\"')\"";

int
rc_ssh_bootstrap_args (const char *ssh, const char *host,
		       const char *args[RC_SSH_ARGS_SIZE])
{
  const char *list[] = { ssh, "--", host, rc_ssh_remote_command, NULL };
  static_assert (sizeof list / sizeof *list <= RC_SSH_ARGS_SIZE,
		 "RC_SSH_ARGS_SIZE is too small");
  return copy_args (list, args);
}

int
rc_ssh_tunnel_args (const char *ssh, const char *host, const char *socket,
		    uint16_t port, char *forward, size_t size,
		    const char *args[RC_SSH_ARGS_SIZE])
{
  int length = snprintf (forward, size, "%s:127.0.0.1:%u", socket,
			 (unsigned) port);
  if (length < 0 || (size_t) length >= size)
    return -1;
  const char *list[] =
    { ssh, "-N", "-o", "ExitOnForwardFailure=yes",
      "-o", "ServerAliveInterval=5", "-o", "ServerAliveCountMax=2",
      "-L", forward, "--", host, NULL };
  static_assert (sizeof list / sizeof *list <= RC_SSH_ARGS_SIZE,
		 "RC_SSH_ARGS_SIZE is too small");
  return copy_args (list, args);
}

/* Copy the arguments of LIST and its null pointer to ARGS.  Return the
   number of arguments.  */
static int
copy_args (const char *const *list, const char **args)
{
  int count = 0;
  for (; list[count]; count++)
    args[count] = list[count];
  args[count] = NULL;
  return count;
}
