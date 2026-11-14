/* The relay of remote-client.

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

/* SSH starts the relay on the computer of the session.  The relay
   listens on a new port of the loopback address, and writes the line
   of relayline.h to its standard output.  Then it closes its standard
   output, so SSH can stop, and the relay keeps running.

   A client connects to the port and sends the token.  The relay then
   connects to the Unix-domain socket of the session, and copies the
   bytes between the client and the session.  A new client with the
   token replaces the old client, as the session also does.  When the
   relay cannot connect to the session, the session has stopped, so
   the relay exits.

   The relay reads and writes the connection of the client with read
   and send.  The task of TLS will put a TLS session on that
   connection.  That task adds the interface of a stream, because
   until then the relay has only one kind of connection.  */

#ifndef REMOTE_CLIENT_RELAY_H
#define REMOTE_CLIENT_RELAY_H

/* The relay closes a connection that does not send the token in this
   number of milliseconds.  */
enum { RC_RELAY_TOKEN_LIMIT = 5000 };

/* Run the relay of the session socket SESSION.  Return the exit status
   of the program.  The process that returns is not the process that
   called this function: the relay starts a new process, so it does
   not stop with SSH.  */
extern int rc_relay_run (const char *session);

#endif /* REMOTE_CLIENT_RELAY_H */
