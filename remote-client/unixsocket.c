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

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "progname.h"
#include "unixsocket.h"

int
rc_connect_unix (const char *path, bool report)
{
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  if (strlen (path) >= sizeof address.sun_path)
    {
      if (report)
	fprintf (stderr, RC_PROGRAM ": %s: the name is too long\n", path);
      return -1;
    }
  strcpy (address.sun_path, path);
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0
      || connect (fd, (struct sockaddr *) &address, sizeof address) != 0)
    {
      if (report)
	fprintf (stderr, RC_PROGRAM ": %s: %s\n", path, strerror (errno));
      if (fd >= 0)
	close (fd);
      return -1;
    }
  return fd;
}
