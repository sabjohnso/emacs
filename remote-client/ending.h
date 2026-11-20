/* The end of a session for the remote display client.

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

/* The server, or the relay, ends a session with an end-session
   message (rdproto.h, the session block).  These functions decide
   what the program does then.  They need no display.  */

#ifndef REMOTE_CLIENT_ENDING_H
#define REMOTE_CLIENT_ENDING_H

#include <stdbool.h>
#include <stdint.h>

/* Return the exit status of the program after an end-session message
   with the reason REASON: 0 for a session that ended on purpose, and
   1 for each other reason.  */
extern int rc_end_status (uint8_t reason);

/* Return the exit status of the window mode at its end.  ENDED is true
   after an end-session message with the reason REASON; then the status
   is that of REASON.  Else the status is 1 if FAILED, that is, if the
   last connection had an error, and 0 if not.  */
extern int rc_exit_status (bool ended, uint8_t reason, bool failed);

#endif /* REMOTE_CLIENT_ENDING_H */
