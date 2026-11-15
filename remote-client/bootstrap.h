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

/* The bootstrap runs SSH with the remote command of sshargs.h.  The
   remote command starts a session and its relay on HOST, and the relay
   writes its line.  SSH gives the line to the client, and stops.

   Before the bootstrap, the client can run "ssh -G HOST" to get the
   host name that SSH connects to: see sshconfig.h.  */

#ifndef REMOTE_CLIENT_BOOTSTRAP_H
#define REMOTE_CLIENT_BOOTSTRAP_H

#include <stdbool.h>

#include "listenspec.h"
#include "relayline.h"
#include "sshconfig.h"

/* The longest wait for the line, in milliseconds.  SSH can ask the
   user for a password in this time.  */
enum { RC_BOOTSTRAP_LIMIT = 120000 };

/* Run the bootstrap on HOST with the program SSH, and put the line of
   the relay in LINE.  With SPEC, the relay listens at the place of
   SPEC.  With NULL, it listens on the server address of
   $SSH_CONNECTION on HOST.  Return false on an error, and write a
   message to the standard error output.  */
extern bool rc_bootstrap (const char *ssh, const char *host,
			  const struct rc_listen_spec *spec,
			  struct rc_relay_line *line);

/* Run "ssh -G HOST" with the program SSH, and put the host name that
   SSH connects to in NAME.  Wait at most RC_BOOTSTRAP_LIMIT
   milliseconds.  Return false on an error, and write a message to the
   standard error output.  */
extern bool rc_bootstrap_hostname (const char *ssh, const char *host,
				   char name[RC_SSH_HOSTNAME_SIZE]);

#endif /* REMOTE_CLIENT_BOOTSTRAP_H */
