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

/* The parts of the remote command before and after the --listen.  */
static const char command_start[] = "remote-client --relay";
static const char command_end[]
  = " \"$(emacsclient --eval '(remote-display-start)' | tr -d '\"')\"";

void
rc_ssh_remote_command (const struct rc_listen_spec *spec,
		       char command[RC_SSH_COMMAND_SIZE])
{
  static_assert (sizeof command_start + sizeof command_end
		 + sizeof " --listen=" + RC_LISTEN_TEXT_SIZE
		 <= RC_SSH_COMMAND_SIZE, "RC_SSH_COMMAND_SIZE is too small");
  char listen[sizeof " --listen=" + RC_LISTEN_TEXT_SIZE] = "";
  if (spec)
    {
      char format[RC_LISTEN_TEXT_SIZE];
      rc_listen_spec_format (spec, format);
      snprintf (listen, sizeof listen, " --listen=%s", format);
    }
  snprintf (command, RC_SSH_COMMAND_SIZE, "%s%s%s", command_start, listen,
	    command_end);
}

int
rc_ssh_bootstrap_args (const char *ssh, const char *host,
		       const char *command,
		       const char *args[RC_SSH_ARGS_SIZE])
{
  const char *list[] = { ssh, "--", host, command, NULL };
  static_assert (sizeof list / sizeof *list <= RC_SSH_ARGS_SIZE,
		 "RC_SSH_ARGS_SIZE is too small");
  return copy_args (list, args);
}

int
rc_ssh_config_args (const char *ssh, const char *host,
		    const char *args[RC_SSH_ARGS_SIZE])
{
  const char *list[] = { ssh, "-G", "--", host, NULL };
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
