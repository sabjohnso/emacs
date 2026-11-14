/* The arguments of SSH for remote-client.

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

/* "remote-client --ssh HOST" runs SSH two times.  The bootstrap runs
   the remote command on HOST: it starts a session and its relay, and
   SSH gives the line of the relay to the client.  The tunnel forwards
   a local socket to the port of the relay on HOST, until the relay
   uses TLS.

   "--" comes before HOST, so a HOST that starts with "-" is not an
   option of SSH.  */

#ifndef REMOTE_CLIENT_SSHARGS_H
#define REMOTE_CLIENT_SSHARGS_H

#include <stddef.h>
#include <stdint.h>

/* The most arguments, with the null pointer at the end.  */
enum { RC_SSH_ARGS_SIZE = 16 };

/* A buffer of this size holds the forward of a socket name of up to
   108 bytes.  */
enum { RC_SSH_FORWARD_SIZE = 140 };

/* The remote command of the bootstrap.  It starts a session in the
   Emacs server of HOST, and gives its socket to the relay.  */
extern const char rc_ssh_remote_command[];

/* Put the arguments of the bootstrap on HOST with the program SSH in
   ARGS, and a null pointer after them.  Return their number.  */
extern int rc_ssh_bootstrap_args (const char *ssh, const char *host,
				  const char *args[RC_SSH_ARGS_SIZE]);

/* Put the arguments of a tunnel from the local SOCKET to PORT of the
   loopback address of HOST in ARGS, and a null pointer after them.
   The text of the forward goes in FORWARD, a buffer of SIZE bytes.
   Return the number of arguments, or -1 if the forward does not fit.
   SSH stops when it cannot make the forward, and when HOST does not
   answer for about 10 seconds.  */
extern int rc_ssh_tunnel_args (const char *ssh, const char *host,
			       const char *socket, uint16_t port,
			       char *forward, size_t size,
			       const char *args[RC_SSH_ARGS_SIZE]);

#endif /* REMOTE_CLIENT_SSHARGS_H */
