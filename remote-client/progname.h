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

/* RC_PROGRAM is the name of the program, as a string.  The Makefile
   defines it from PROGRAM, so the file of the program and its
   messages have the same name.  The messages and the usage of the
   program start with this name.  The remote command that the client
   gives to SSH runs the relay with this name, so the relay must have
   this name in the PATH of the host.  */
#ifndef RC_PROGRAM
# error "RC_PROGRAM is not defined; the Makefile defines it from PROGRAM"
#endif

/* RC_VERSION is the version of Emacs, as a string.  The Makefile
   defines it from the version that configure gives.  */
#ifndef RC_VERSION
# error "RC_VERSION is not defined; the Makefile defines it from version"
#endif

/* RC_ICON is the name of the icon of the windows in the icon theme, as
   a string.  The Makefile defines it from ICON.  */
#ifndef RC_ICON
# error "RC_ICON is not defined; the Makefile defines it from ICON"
#endif

#endif /* RC_PROGNAME_H */
