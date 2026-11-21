/* The name of the program of the client of a remote display.

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

#ifndef RC_PROGNAME_H
#define RC_PROGNAME_H

/* The name of the program.  The messages and the usage of the program
   start with this name.  The remote command that the client gives to
   SSH runs the relay with this name, so the relay must have this name
   in the PATH of the host.  */
#define RC_PROGRAM "remote-client"

#endif /* RC_PROGNAME_H */
