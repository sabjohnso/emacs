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
   writes its line.  SSH gives the line to the client, and stops.  */

#ifndef REMOTE_CLIENT_BOOTSTRAP_H
#define REMOTE_CLIENT_BOOTSTRAP_H

#include <stdbool.h>

#include "relayline.h"

/* The longest wait for the line, in milliseconds.  SSH can ask the
   user for a password in this time.  */
enum { RC_BOOTSTRAP_LIMIT = 120000 };

/* Run the bootstrap on HOST with the program SSH, and put the line of
   the relay in LINE.  Return false on an error, and write a message to
   the standard error output.  */
extern bool rc_bootstrap (const char *ssh, const char *host,
			  struct rc_relay_line *line);

#endif /* REMOTE_CLIENT_BOOTSTRAP_H */
