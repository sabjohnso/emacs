/* The input messages of the remote display client.

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

/* This module makes the messages of the input of the user from the
   values of GDK events.  It needs no display, so a test can use it
   with no display.  */

#ifndef REMOTE_CLIENT_INPUT_H
#define REMOTE_CLIENT_INPUT_H

#include <stdint.h>

#include <gdk/gdk.h>

#include "rdproto.h"

/* Return the key message of a press of the key KEYVAL, with the GDK
   modifier state STATE, at the time TIME, in the frame FRAME.  */
extern struct rd_message rc_key_message (uint32_t frame, guint keyval,
					 GdkModifierType state,
					 guint32 time);

#endif /* REMOTE_CLIENT_INPUT_H */
