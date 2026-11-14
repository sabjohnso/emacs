/* The TLS streams of remote-client.


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

/* The relay and the client speak TLS 1.3 through GnuTLS.

   The relay makes a new key when it starts.  The key has a
   self-signed certificate, and it stays in memory only.  The pin of
   pin.h is the SHA-256 hash of the public key, in the DER form of a
   SubjectPublicKeyInfo.  The client gets the pin through SSH, and
   accepts only a server with that key.  It does not check the
   certificate in other ways, because no certificate authority signs
   it.

   The streams have the functions of stream.h.  */

#ifndef REMOTE_CLIENT_TLS_H
#define REMOTE_CLIENT_TLS_H

#include <stdint.h>

#include "pin.h"
#include "stream.h"

struct rc_tls_key;

/* Return a new key with its certificate, or NULL on an error.  */
extern struct rc_tls_key *rc_tls_key_new (void);

/* Put the pin of KEY in PIN.  */
extern void rc_tls_key_pin (const struct rc_tls_key *key,
			    uint8_t pin[RC_PIN_SIZE]);

/* Free KEY.  No stream of KEY can be open.  */
extern void rc_tls_key_free (struct rc_tls_key *key);

/* Return a new stream of the server with KEY on the connected socket
   FD, which does not block.  The handshake goes on in the reads and
   writes of the stream, which give RC_STREAM_WAIT until it is
   complete.  The stream owns FD.  On an error, close FD and return
   NULL.  */
extern struct rc_stream *rc_tls_accept (const struct rc_tls_key *key,
					int fd);

/* Do the handshake of a client on the connected socket FD, which
   blocks, in at most LIMIT milliseconds.  If the key of the server has
   the pin PIN, return the new stream.  The stream owns FD.  Else
   close FD and return NULL.  */
extern struct rc_stream *rc_tls_connect (int fd,
					 const uint8_t pin[RC_PIN_SIZE],
					 int limit);

#endif /* REMOTE_CLIENT_TLS_H */
