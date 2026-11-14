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

#ifndef REMOTE_CLIENT_CHILD_H
#define REMOTE_CLIENT_CHILD_H

/* In a new child process, run PROGRAM with the arguments ARGS, which
   end with a null pointer.  First close each descriptor other than the
   standard streams, so the program does not get the connections and
   the display of the client.  This function does not return.  */
extern void rc_child_exec (const char *program, const char *const *args)
  __attribute__ ((noreturn));

#endif /* REMOTE_CLIENT_CHILD_H */
