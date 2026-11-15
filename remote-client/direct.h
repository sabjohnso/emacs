/* The direct connection of remote-client to a relay.

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

/* The client connects to the relay at the host name of "ssh -G", or
   at the address of --connect, and at the port of the line of the
   relay.  The direct connector resolves the name at each connect,
   because a change of network can change the answer.  It tries each
   address of the answer, makes a TLS session with the key of the pin
   of the line, and sends the token of the line first.

   The direct connector is a connector of connector.h.  A connect waits
   at most RC_DIRECT_LIMIT milliseconds for the connection, and then
   at most RC_RELAY_TOKEN_LIMIT milliseconds for the TLS session.  */

#ifndef REMOTE_CLIENT_DIRECT_H
#define REMOTE_CLIENT_DIRECT_H

#include "relayline.h"
#include "stream.h"

/* The longest wait for the connection to one address, in
   milliseconds.  */
enum { RC_DIRECT_LIMIT = 5000 };

struct rc_direct;

/* Return a new direct connector to the relay of LINE at HOST.  HOST is
   a name or a numeric address.  Return NULL if there is no memory.  */
extern struct rc_direct *rc_direct_new (const char *host,
					const struct rc_relay_line *line);

/* The connect function of the direct connector DATA.  Return a new
   stream to the relay, or NULL.  */
extern struct rc_stream *rc_direct_connect (void *data);

/* Free DIRECT.  DIRECT can be NULL.  */
extern void rc_direct_free (struct rc_direct *direct);

#endif /* REMOTE_CLIENT_DIRECT_H */
