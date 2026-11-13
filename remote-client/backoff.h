/* The schedule of the reconnects of remote-client.

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

/* After the client loses its connection, it connects again.  When a
   try fails, the client waits longer before the next try, so a client
   with no network does not use the processor and the network all the
   time.  The first wait is RC_BACKOFF_FIRST milliseconds.  Each next
   wait is double the wait before it, up to RC_BACKOFF_LIMIT
   milliseconds.  When the server sends bytes on a new connection, the
   schedule starts again from RC_BACKOFF_FIRST.

   A struct rc_backoff gives the waits.  It needs no clock.  */

#ifndef REMOTE_CLIENT_BACKOFF_H
#define REMOTE_CLIENT_BACKOFF_H

#include <stdint.h>

enum { RC_BACKOFF_FIRST = 250, RC_BACKOFF_LIMIT = 30000 };

/* The schedule of the waits.  Only the functions of backoff.c use the
   fields.  */
struct rc_backoff
{
  int64_t next;
};

/* Return a schedule whose next wait is RC_BACKOFF_FIRST.  */
extern struct rc_backoff rc_backoff_new (void);

/* Return the next wait of BACKOFF in milliseconds, and make the wait
   after it longer.  */
extern int64_t rc_backoff_next (struct rc_backoff *backoff);

/* Make the next wait of BACKOFF RC_BACKOFF_FIRST again.  */
extern void rc_backoff_reset (struct rc_backoff *backoff);

#endif /* REMOTE_CLIENT_BACKOFF_H */
