/* The SSH tunnel of remote-client to a relay.

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

/* Until the task of the listen address, the relay listens only on the
   loopback address of its computer.  A tunnel of SSH forwards a local socket to
   the port of the relay.  The local socket is in a new directory that
   only the user can use.

   The tunnel is a connector of connector.h.  Each connect starts SSH
   again if it has stopped, for example after a change of network.
   Then the connect fails, and the window mode tries again later.  A
   connect makes a TLS session with the key of the pin of the relay,
   and sends the token of the relay first.  */

#ifndef REMOTE_CLIENT_TUNNEL_H
#define REMOTE_CLIENT_TUNNEL_H

#include <stdint.h>

#include "relayline.h"
#include "stream.h"

/* The longest wait for the first connection, in milliseconds.  SSH can
   ask the user for a password in this time.  */
enum { RC_TUNNEL_FIRST_LIMIT = 120000 };

struct rc_tunnel;

/* Return a new tunnel to the relay of LINE on HOST, through the
   program SSH.  The local socket is in a new directory in the directory
   BASE.  The tunnel does not run yet.  Return NULL on an error, and
   write a message to the standard error output.  */
extern struct rc_tunnel *rc_tunnel_new (const char *ssh, const char *host,
					const struct rc_relay_line *line,
					const char *base);

/* Return the first stream of TUNNEL to the relay.  Wait for SSH at
   most RC_TUNNEL_FIRST_LIMIT milliseconds.  Return NULL, and write a
   message, if SSH stops two times, or does not connect in that
   time.  */
extern struct rc_stream *rc_tunnel_first_connect (struct rc_tunnel *tunnel);

/* The connect function of the connector of the tunnel DATA.  Return a
   new stream to the relay, or NULL.  */
extern struct rc_stream *rc_tunnel_connect (void *data);

/* Stop SSH, remove the local socket and its directory, and free
   TUNNEL.  TUNNEL can be NULL.  */
extern void rc_tunnel_free (struct rc_tunnel *tunnel);

#endif /* REMOTE_CLIENT_TUNNEL_H */
