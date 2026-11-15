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

/* "remote-client --ssh HOST" runs SSH two times.  The bootstrap runs
   the remote command on HOST: it starts a session and its relay, and
   SSH gives the line of the relay to the client.  "ssh -G HOST" gives
   the configuration of HOST, so the client finds the name that SSH
   connects to.

   "--" comes before HOST, so a HOST that starts with "-" is not an
   option of SSH.  */

#ifndef REMOTE_CLIENT_SSHARGS_H
#define REMOTE_CLIENT_SSHARGS_H

#include "listenspec.h"

/* The most arguments, with the null pointer at the end.  */
enum { RC_SSH_ARGS_SIZE = 16 };

/* The size of a buffer for the remote command, with its null byte.  */
enum { RC_SSH_COMMAND_SIZE = 96 + sizeof " --listen=" + RC_LISTEN_TEXT_SIZE };

/* Write the remote command of the bootstrap and a null byte to
   COMMAND.  The command starts a session in the Emacs server of HOST,
   and gives its socket to the relay.  With SPEC, the relay listens at
   the place of SPEC.  With NULL, it listens on the server address of
   $SSH_CONNECTION.  The format of SPEC is safe in a shell.  */
extern void rc_ssh_remote_command (const struct rc_listen_spec *spec,
				   char command[RC_SSH_COMMAND_SIZE]);

/* Put the arguments of the bootstrap on HOST with the program SSH and
   the remote command COMMAND in ARGS, and a null pointer after them.
   Return their number.  */
extern int rc_ssh_bootstrap_args (const char *ssh, const char *host,
				  const char *command,
				  const char *args[RC_SSH_ARGS_SIZE]);

/* Put the arguments of "ssh -G HOST" with the program SSH in ARGS, and
   a null pointer after them.  Return their number.  */
extern int rc_ssh_config_args (const char *ssh, const char *host,
			       const char *args[RC_SSH_ARGS_SIZE]);

#endif /* REMOTE_CLIENT_SSHARGS_H */
