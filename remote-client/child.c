/* The start of a program in a child process of remote-client.

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

#include <stdio.h>
#include <unistd.h>

#include "child.h"
#include "progname.h"

void
rc_child_exec (const char *program, const char *const *args)
{
  closefrom (STDERR_FILENO + 1);
  execvp (program, (char *const *) args);
  perror (RC_PROGRAM ": ssh");
  _exit (127);
}
