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

/* This codec does not use Lisp objects, so the server (Emacs) and
   the client can both use it.

   A message on the wire is a frame:

     length  4 bytes, big-endian: the number of bytes that follow
     type    1 byte: an enum rd_type value
     body    LENGTH - 1 bytes: the fields of the message type

   Each field is big-endian.  A later version of the protocol can
   append fields to a message type.  A decoder reads the fields that
   it knows, and ignores the bytes that follow them.  LENGTH is at
   most RD_MAX_LENGTH.  */

#ifndef EMACS_RDPROTO_H
#define EMACS_RDPROTO_H

#include <stddef.h>
#include <stdint.h>

enum { RD_MAX_LENGTH = 1 << 24 };

enum rd_type
  {
    RD_HELLO = 1,
  };

/* The first message of each side of a connection.  */
struct rd_hello
{
  uint16_t version;
};

struct rd_message
{
  enum rd_type type;
  union
  {
    struct rd_hello hello;
  } u;
};

enum rd_status
  {
    /* BUF holds a complete message.  */
    RD_OK,
    /* BUF holds only the start of a message.  Read more bytes.  */
    RD_INCOMPLETE,
    /* The type byte is not an enum rd_type value.  */
    RD_UNKNOWN_TYPE,
    /* The length is too small for the type.  */
    RD_BAD_LENGTH,
    /* The length is more than RD_MAX_LENGTH.  */
    RD_FRAME_TOO_LONG,
  };

/* Return the number of bytes that rd_encode writes for MESSAGE.  */
extern size_t rd_encoded_size (const struct rd_message *message);

/* Write MESSAGE to BUF, which has rd_encoded_size (MESSAGE) bytes.  */
extern void rd_encode (const struct rd_message *message,
		       unsigned char *buf);

/* Decode the first message in the LEN bytes at BUF.  If the result
   is RD_OK, set *MESSAGE to the message and *USED to the number of
   bytes that it occupies.  Report an error as soon as the length
   and the type byte are present, before the rest of the frame.  */
extern enum rd_status rd_decode (const unsigned char *buf, size_t len,
				 struct rd_message *message, size_t *used);

#endif /* EMACS_RDPROTO_H */
