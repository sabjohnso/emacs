/* The input from the client of a remote display session.

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

#ifndef EMACS_REMOTEINPUT_H
#define EMACS_REMOTEINPUT_H

#include "rdproto.h"
#include "remoteterm.h"

/* Give the input message MESSAGE of the client of DPYINFO to Emacs.
   Store the event of the message, if it has one.  */
extern void remote_handle_input (struct remote_display_info *dpyinfo,
				 const struct rd_message *message);

#endif /* EMACS_REMOTEINPUT_H */
