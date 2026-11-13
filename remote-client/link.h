/* The liveness of the connection of remote-client to its server.

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

/* A connection can stop with no end of file.  For example, after a
   change of network or a sleep of the laptop, the packets of the
   server do not come, and the kernel does not tell the client.  Thus
   the client watches the time since the last bytes from the server.

   When the connection is quiet for the ping interval, the client
   sends a ping, and the server answers with a pong.  When no bytes
   come for the timeout, the connection is lost.  A change of network
   makes the client send a ping at once.

   A struct rc_link decides this for one connection.  It needs no
   clock and no display: the caller gives it the time of each event,
   in milliseconds of a monotonic clock.  */

#ifndef REMOTE_CLIENT_LINK_H
#define REMOTE_CLIENT_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* The default timeout in milliseconds.  */
enum { RC_LINK_DEFAULT_TIMEOUT = 15000 };

/* The ping interval is the timeout divided by this number, so a lost
   connection has this number of pings with no answer.  */
enum { RC_LINK_PINGS_PER_TIMEOUT = 3 };

/* What the client must do after a tick.  */
enum rc_link_action
  {
    /* Nothing.  */
    RC_LINK_WAIT,
    /* Send a ping.  */
    RC_LINK_PING,
    /* The connection is lost.  */
    RC_LINK_LOST,
  };

/* The liveness of one connection.  Only the functions of link.c use
   the fields.  */
struct rc_link
{
  int64_t timeout;
  int64_t last_input;
  int64_t last_ping;
  bool ping_now;
  bool lost;
  uint32_t serial;
};

/* Return the liveness of a connection that opened at the time NOW,
   with TIMEOUT milliseconds.  TIMEOUT must be at least
   RC_LINK_PINGS_PER_TIMEOUT.  */
extern struct rc_link rc_link_new (int64_t now, int64_t timeout);

/* Record in LINK that bytes came from the server at the time NOW.  */
extern void rc_link_note_input (struct rc_link *link, int64_t now);

/* Record in LINK that the network changed.  */
extern void rc_link_note_network_change (struct rc_link *link);

/* Return the longest time in milliseconds between two calls of
   rc_link_tick for a connection with TIMEOUT milliseconds: a third of
   the ping interval, from 1 millisecond to 1 second.  */
extern int64_t rc_link_tick_interval (int64_t timeout);

/* Return what the client must do at the time NOW.  The caller calls
   this function at least once in each rc_link_tick_interval of the
   timeout of LINK.  If the result is RC_LINK_PING, set *SERIAL to the
   serial of the ping.  After a result of RC_LINK_LOST, each later
   result is RC_LINK_LOST.  */
extern enum rc_link_action rc_link_tick (struct rc_link *link, int64_t now,
					 uint32_t *serial);

#endif /* REMOTE_CLIENT_LINK_H */
