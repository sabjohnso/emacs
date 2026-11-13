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

#include <config.h>

#include "link.h"

static int64_t ping_interval (const struct rc_link *);

struct rc_link
rc_link_new (int64_t now, int64_t timeout)
{
  return (struct rc_link) { .timeout = timeout, .last_input = now,
			    .last_ping = now };
}

void
rc_link_note_input (struct rc_link *link, int64_t now)
{
  link->last_input = now;
}

void
rc_link_note_network_change (struct rc_link *link)
{
  link->ping_now = true;
}

int64_t
rc_link_tick_interval (int64_t timeout)
{
  int64_t third = timeout / RC_LINK_PINGS_PER_TIMEOUT / 3;
  return third < 1 ? 1 : third > 1000 ? 1000 : third;
}

enum rc_link_action
rc_link_tick (struct rc_link *link, int64_t now, uint32_t *serial)
{
  if (link->lost || now - link->last_input >= link->timeout)
    {
      link->lost = true;
      return RC_LINK_LOST;
    }
  int64_t quiet_since = (link->last_input > link->last_ping
			 ? link->last_input : link->last_ping);
  if (!link->ping_now && now - quiet_since < ping_interval (link))
    return RC_LINK_WAIT;
  link->ping_now = false;
  link->last_ping = now;
  *serial = ++link->serial;
  return RC_LINK_PING;
}

/* Return the time in milliseconds from the last bytes of the
   connection to the next ping.  */
static int64_t
ping_interval (const struct rc_link *link)
{
  return link->timeout / RC_LINK_PINGS_PER_TIMEOUT;
}
