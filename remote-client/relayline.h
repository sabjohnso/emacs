/* The line that the relay of remote-client gives to the client.

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

/* When the relay starts, it writes one line to its standard output.
   SSH gives the line to the client.  The line tells the client how to
   connect to the relay.  Its text is

     remote-display-relay 2 PORT TOKEN PIN

   and a newline.  2 is the version of the line.  PORT is the port of
   the relay in decimal, from 1 to 65535, with no zero before it.
   TOKEN is the token and PIN is the pin of pin.h, in hexadecimal, with
   lowercase letters.  Each line has only one text, so a parse accepts
   only that text.  */

#ifndef REMOTE_CLIENT_RELAYLINE_H
#define REMOTE_CLIENT_RELAYLINE_H

#include <stdbool.h>
#include <stdint.h>

#include "pin.h"
#include "token.h"

/* The size of a buffer for the text of a line, with its null byte.  */
enum { RC_RELAY_LINE_SIZE = 160 };

/* What the client must know to connect to the relay.  */
struct rc_relay_line
{
  uint16_t port;
  uint8_t token[RC_TOKEN_SIZE];
  uint8_t pin[RC_PIN_SIZE];
};

/* Write the text of LINE and a null byte to TEXT.  The port of LINE
   must not be 0.  */
extern void rc_relay_line_format (const struct rc_relay_line *line,
				  char text[RC_RELAY_LINE_SIZE]);

/* If TEXT is the text of a line, put the line in LINE and return true.
   Otherwise, return false, and do not change LINE.  */
extern bool rc_relay_line_parse (const char *text,
				 struct rc_relay_line *line);

#endif /* REMOTE_CLIENT_RELAYLINE_H */
