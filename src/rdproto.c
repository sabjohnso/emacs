/* Message codec of the remote display protocol.

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

#include <stdlib.h>

#include "rdproto.h"

/* The size of the length field at the start of each frame.  */
enum { LENGTH_SIZE = 4 };

/* The size of the type byte and the fields of each message type.  */
enum { HELLO_SIZE = 1 + 2 };

static size_t fields_size (unsigned int);
static void put_uint16 (unsigned char *, uint16_t);
static void put_uint32 (unsigned char *, uint32_t);
static uint16_t get_uint16 (const unsigned char *);
static uint32_t get_uint32 (const unsigned char *);

size_t
rd_encoded_size (const struct rd_message *message)
{
  size_t size = fields_size (message->type);
  if (size == 0)
    abort ();
  return LENGTH_SIZE + size;
}

void
rd_encode (const struct rd_message *message, unsigned char *buf)
{
  put_uint32 (buf, rd_encoded_size (message) - LENGTH_SIZE);
  buf[LENGTH_SIZE] = message->type;
  switch (message->type)
    {
    case RD_HELLO:
      put_uint16 (buf + LENGTH_SIZE + 1, message->u.hello.version);
      break;
    }
}

enum rd_status
rd_decode (const unsigned char *buf, size_t len,
	   struct rd_message *message, size_t *used)
{
  if (len < LENGTH_SIZE)
    return RD_INCOMPLETE;
  uint32_t length = get_uint32 (buf);
  if (length > RD_MAX_LENGTH)
    return RD_FRAME_TOO_LONG;
  if (length < 1)
    return RD_BAD_LENGTH;
  if (len == LENGTH_SIZE)
    return RD_INCOMPLETE;

  unsigned int type = buf[LENGTH_SIZE];
  size_t size = fields_size (type);
  if (size == 0)
    return RD_UNKNOWN_TYPE;
  if (length < size)
    return RD_BAD_LENGTH;
  if (len - LENGTH_SIZE < length)
    return RD_INCOMPLETE;

  const unsigned char *fields = buf + LENGTH_SIZE + 1;
  switch (type)
    {
    case RD_HELLO:
      message->type = RD_HELLO;
      message->u.hello.version = get_uint16 (fields);
      break;
    }
  *used = LENGTH_SIZE + length;
  return RD_OK;
}

/* Return the size of the type byte and the fields of a TYPE message,
   or 0 if TYPE is not a message type.  */
static size_t
fields_size (unsigned int type)
{
  switch (type)
    {
    case RD_HELLO:
      return HELLO_SIZE;
    default:
      return 0;
    }
}

static void
put_uint16 (unsigned char *p, uint16_t n)
{
  p[0] = n >> 8;
  p[1] = n;
}

static void
put_uint32 (unsigned char *p, uint32_t n)
{
  p[0] = n >> 24;
  p[1] = n >> 16;
  p[2] = n >> 8;
  p[3] = n;
}

static uint16_t
get_uint16 (const unsigned char *p)
{
  return ((uint16_t) p[0] << 8) | (uint16_t) p[1];
}

static uint32_t
get_uint32 (const unsigned char *p)
{
  return (((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16)
	  | ((uint32_t) p[2] << 8) | (uint32_t) p[3]);
}
