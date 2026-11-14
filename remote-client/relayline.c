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

#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "relayline.h"

/* The text before the port.  */
#define PREFIX "remote-display-relay 2 "

/* The longest text: the prefix, 5 digits, a space, the token, a
   space, the pin, a newline, and a null byte.  */
static_assert (RC_RELAY_LINE_SIZE
	       >= (sizeof PREFIX - 1 + 5 + 1 + 2 * RC_TOKEN_SIZE + 1
		   + 2 * RC_PIN_SIZE + 1 + 1),
	       "RC_RELAY_LINE_SIZE is too small");

static int format_hex (char *, const uint8_t *, size_t);
static const char *parse_port (const char *, uint16_t *);
static const char *parse_hex (const char *, uint8_t *, size_t);
static int hex_digit (char);

void
rc_relay_line_format (const struct rc_relay_line *line,
		      char text[RC_RELAY_LINE_SIZE])
{
  assert (line->port != 0);
  int length = sprintf (text, PREFIX "%u ", (unsigned) line->port);
  length += format_hex (text + length, line->token, RC_TOKEN_SIZE);
  text[length++] = ' ';
  length += format_hex (text + length, line->pin, RC_PIN_SIZE);
  strcpy (text + length, "\n");
}

bool
rc_relay_line_parse (const char *text, struct rc_relay_line *line)
{
  struct rc_relay_line parsed;
  if (strncmp (text, PREFIX, strlen (PREFIX)) != 0)
    return false;
  const char *rest = parse_port (text + strlen (PREFIX), &parsed.port);
  if (!rest || *rest++ != ' ')
    return false;
  rest = parse_hex (rest, parsed.token, RC_TOKEN_SIZE);
  if (!rest || *rest++ != ' ')
    return false;
  rest = parse_hex (rest, parsed.pin, RC_PIN_SIZE);
  if (!rest || strcmp (rest, "\n") != 0)
    return false;
  *line = parsed;
  return true;
}

/* Write the SIZE BYTES to TEXT in hexadecimal, with lowercase
   letters, and return the number of characters.  */
static int
format_hex (char *text, const uint8_t *bytes, size_t size)
{
  for (size_t i = 0; i < size; i++)
    sprintf (text + 2 * i, "%02x", bytes[i]);
  return 2 * size;
}

/* Parse the port at TEXT into PORT.  Return the text after the port,
   or NULL if TEXT has no valid port.  */
static const char *
parse_port (const char *text, uint16_t *port)
{
  if (*text < '1' || *text > '9')
    return NULL;
  unsigned long value = 0;
  for (; *text >= '0' && *text <= '9'; text++)
    {
      value = value * 10 + (*text - '0');
      if (value > 65535)
	return NULL;
    }
  *port = value;
  return text;
}

/* Parse SIZE bytes in hexadecimal at TEXT into BYTES.  Return the
   text after them, or NULL if TEXT does not start with them.  */
static const char *
parse_hex (const char *text, uint8_t *bytes, size_t size)
{
  for (size_t i = 0; i < size; i++, text += 2)
    {
      int high = hex_digit (text[0]);
      int low = high < 0 ? -1 : hex_digit (text[1]);
      if (low < 0)
	return NULL;
      bytes[i] = high << 4 | low;
    }
  return text;
}

/* Return the value of the lowercase hexadecimal digit DIGIT, or -1 if
   DIGIT is not one.  */
static int
hex_digit (char digit)
{
  if (digit >= '0' && digit <= '9')
    return digit - '0';
  if (digit >= 'a' && digit <= 'f')
    return digit - 'a' + 10;
  return -1;
}
