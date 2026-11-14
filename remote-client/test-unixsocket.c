/* Test the connection to a Unix-domain socket of remote-client.

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

/* The examples of rc_connect_unix:

   - Connect: a connection to a socket that listens works.
   - Missing: a connection to a name with no socket fails.
   - Long: a connection to a name that does not fit in the address
     fails.

   A failure writes a message to the standard error output only if the
   caller asks for the report.  */

#include <config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "unixsocket.h"

static bool connect_example (void);
static bool missing_example (void);
static bool long_example (void);
static bool fails_p (const char *);
static long reported (const char *, bool, int *);

/* An example and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} examples[] =
  {
    { "connect", connect_example },
    { "missing", missing_example },
    { "long", long_example },
  };

int
main (void)
{
  for (size_t i = 0; i < sizeof examples / sizeof *examples; i++)
    if (!examples[i].check ())
      {
	fprintf (stderr, "test-unixsocket: the %s example failed\n",
		 examples[i].name);
	return 1;
      }
  printf ("test-unixsocket: %zu examples passed\n",
	  sizeof examples / sizeof *examples);
  return 0;
}

static bool
connect_example (void)
{
  char directory[] = "/tmp/test-unixsocket-XXXXXX";
  if (!mkdtemp (directory))
    return false;
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  snprintf (address.sun_path, sizeof address.sun_path, "%s/socket",
	    directory);
  int listener = socket (AF_UNIX, SOCK_STREAM, 0);
  bool ok = (listener >= 0
	     && bind (listener, (struct sockaddr *) &address,
		      sizeof address) == 0
	     && listen (listener, 1) == 0);
  int fd = -1;
  ok = ok && reported (address.sun_path, true, &fd) == 0 && fd >= 0;
  if (fd >= 0)
    close (fd);
  close (listener);
  unlink (address.sun_path);
  rmdir (directory);
  return ok;
}

static bool
missing_example (void)
{
  return fails_p ("/nonexistent/socket");
}

static bool
long_example (void)
{
  char name[sizeof ((struct sockaddr_un *) 0)->sun_path + 10];
  memset (name, 'x', sizeof name - 1);
  name[0] = '/';
  name[sizeof name - 1] = 0;
  return fails_p (name);
}

/* Return true if a connection to PATH fails, with a message when the
   caller asks for the report, and with no message when not.  */
static bool
fails_p (const char *path)
{
  int quiet, loud;
  return (reported (path, false, &quiet) == 0 && quiet < 0
	  && reported (path, true, &loud) > 0 && loud < 0);
}

/* Call rc_connect_unix with PATH and REPORT, and put its result in *FD.
   Return the number of bytes that it wrote to the standard error
   output, or -1 on an error of the test.  */
static long
reported (const char *path, bool report, int *fd)
{
  FILE *capture = tmpfile ();
  int saved = dup (STDERR_FILENO);
  if (!capture || saved < 0)
    return -1;
  fflush (stderr);
  dup2 (fileno (capture), STDERR_FILENO);
  *fd = rc_connect_unix (path, report);
  fflush (stderr);
  dup2 (saved, STDERR_FILENO);
  close (saved);
  struct stat status;
  long size = fstat (fileno (capture), &status) == 0 ? status.st_size : -1;
  fclose (capture);
  return size;
}
