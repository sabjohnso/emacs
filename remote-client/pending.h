/* The connections of the relay that wait for their token.

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

/* A new connection of the relay must send the token before a
   deadline.  The table holds at most RC_PENDING_SIZE such connections.
   When one more comes, a connection with the earliest deadline goes
   out of the table.

   The table does no input and no output, and it reads no clock: the
   caller gives the time, and closes each connection that goes out of
   the table.  */

#ifndef REMOTE_CLIENT_PENDING_H
#define REMOTE_CLIENT_PENDING_H

#include <stddef.h>
#include <stdint.h>

#include "token.h"

/* The most connections of a table.  */
enum { RC_PENDING_SIZE = 8 };

/* The result of a part of a token.  */
enum rc_pending_result
  {
    /* More bytes of the token must come.  */
    RC_PENDING_MORE,
    /* The connection sent the expected token.  */
    RC_PENDING_GOOD,
    /* The connection sent a wrong token.  */
    RC_PENDING_BAD,
  };

/* A connection that waits for its token.  FD is -1 in a free slot.
   GOT is the number of bytes of TOKEN that came.  */
struct rc_pending
{
  int fd;
  size_t got;
  uint8_t token[RC_TOKEN_SIZE];
  int64_t deadline;
};

/* The table.  Only the functions of pending.c use the field.  */
struct rc_pending_table
{
  struct rc_pending slots[RC_PENDING_SIZE];
};

/* Make TABLE empty.  */
extern void rc_pending_init (struct rc_pending_table *table);

/* Add the connection FD with the time DEADLINE to TABLE.  FD must not
   be negative.  If TABLE was
   full, remove the connection with the earliest deadline, and return
   it.  Otherwise, return -1.  */
extern int rc_pending_add (struct rc_pending_table *table, int fd,
			   int64_t deadline);

/* Return the slot of the connection FD in TABLE, or NULL.  */
extern struct rc_pending *rc_pending_find (struct rc_pending_table *table,
					   int fd);

/* Remove the connection of SLOT from its table.  */
extern void rc_pending_remove (struct rc_pending *slot);

/* Return the space for the next bytes of the token of SLOT, and put
   its size in *SIZE.  */
extern uint8_t *rc_pending_space (struct rc_pending *slot, size_t *size);

/* Add the first COUNT bytes of the space of SLOT to its token.  When
   the token is complete, compare it with EXPECTED.  COUNT must not be
   more than the size of the space.  */
extern enum rc_pending_result rc_pending_fill
  (struct rc_pending *slot, size_t count,
   const uint8_t expected[RC_TOKEN_SIZE]);

/* Return the connection of the slot I of TABLE, or -1 if the slot is
   free.  I is from 0 to RC_PENDING_SIZE - 1.  */
extern int rc_pending_fd (const struct rc_pending_table *table, int i);

/* Return the number of connections in TABLE.  */
extern int rc_pending_count (const struct rc_pending_table *table);

/* Return the milliseconds from NOW to the earliest deadline of TABLE,
   0 if that deadline has passed, or -1 if TABLE is empty.  */
extern int64_t rc_pending_wait (const struct rc_pending_table *table,
				int64_t now);

/* Remove a connection whose deadline is not after NOW from TABLE, and
   return it.  Return -1 if TABLE has no such connection.  */
extern int rc_pending_expired (struct rc_pending_table *table, int64_t now);

#endif /* REMOTE_CLIENT_PENDING_H */
