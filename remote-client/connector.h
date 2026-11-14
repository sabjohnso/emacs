/* How the window mode of remote-client connects to a session.

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

/* The window mode connects again after each lost connection.  A
   struct rc_connector tells it how: through a Unix-domain socket, or
   through an SSH tunnel to a relay.  The window mode does not know
   which.  */

#ifndef REMOTE_CLIENT_CONNECTOR_H
#define REMOTE_CLIENT_CONNECTOR_H

/* CONNECT returns a new connection to the session, or -1 if it cannot
   connect now.  It writes no message, because the window mode tries
   again later.  DATA is its argument.  */
struct rc_connector
{
  int (*connect) (void *data);
  void *data;
};

#endif /* REMOTE_CLIENT_CONNECTOR_H */
