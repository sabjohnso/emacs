/* The token of the relay of remote-client.

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

/* The relay accepts a client only if the client first sends the token
   of the relay.  The relay makes a new random token when it starts,
   and gives it to the client through SSH.  Each user of the computer
   can connect to the port of the relay, but only the user of the SSH
   connection knows the token.  */

#ifndef REMOTE_CLIENT_TOKEN_H
#define REMOTE_CLIENT_TOKEN_H

#include <stdbool.h>
#include <stdint.h>

/* The number of bytes of a token.  */
enum { RC_TOKEN_SIZE = 32 };

/* The relay closes a connection that does not send the token in this
   number of milliseconds, with the handshake of TLS.  */
enum { RC_RELAY_TOKEN_LIMIT = 5000 };

/* Put a new random token in TOKEN.  Return false if the system gives
   no random bytes.  */
extern bool rc_token_new (uint8_t token[RC_TOKEN_SIZE]);

/* Return true if the tokens A and B are equal.  The time of the
   comparison does not change with the position of the first byte
   that differs, so the time tells nothing about the token.  */
extern bool rc_token_equal (const uint8_t a[RC_TOKEN_SIZE],
			    const uint8_t b[RC_TOKEN_SIZE]);

#endif /* REMOTE_CLIENT_TOKEN_H */
