/* A connection to a Unix-domain socket for remote-client.

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

#ifndef REMOTE_CLIENT_UNIXSOCKET_H
#define REMOTE_CLIENT_UNIXSOCKET_H

#include <stdbool.h>

/* Return a connection to the Unix-domain socket PATH, or -1 on an
   error.  If REPORT, write the error to the standard error output.  */
extern int rc_connect_unix (const char *path, bool report);

#endif /* REMOTE_CLIENT_UNIXSOCKET_H */
