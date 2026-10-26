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

/* The maximum number of glyphs in one draw-glyphs message.  The
   server sends a longer run of glyphs in more than one message.  */
enum { RD_MAX_GLYPHS = 512 };

enum rd_type
  {
    RD_HELLO = 1,
    RD_DRAW_GLYPHS = 2,
  };

/* The first message of each side of a connection.  */
struct rd_hello
{
  uint16_t version;
};

/* Draw glyphs of the frame font.  X and Y are the pixel position of
   the start of the baseline.  Each glyph code is a glyph index of
   the font, not a character.  */
struct rd_draw_glyphs
{
  uint32_t frame;
  int16_t x;
  int16_t y;
  uint16_t nglyphs;
  uint32_t glyphs[RD_MAX_GLYPHS];
};

struct rd_message
{
  enum rd_type type;
  union
  {
    struct rd_hello hello;
    struct rd_draw_glyphs draw_glyphs;
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
    /* The length is too small for the type, or a count in the
       message is too large.  */
    RD_BAD_LENGTH,
    /* The length is more than RD_MAX_LENGTH.  */
    RD_FRAME_TOO_LONG,
  };

/* Return the number of bytes that rd_encode writes for MESSAGE.  */
extern size_t rd_encoded_size (const struct rd_message *message);

/* Write MESSAGE to BUF, which has rd_encoded_size (MESSAGE) bytes.
   The values of MESSAGE must be in range, for example NGLYPHS at
   most RD_MAX_GLYPHS.  */
extern void rd_encode (const struct rd_message *message,
		       unsigned char *buf);

/* Decode the first message in the LEN bytes at BUF.  If the result
   is RD_OK, set *MESSAGE to the message and *USED to the number of
   bytes that it occupies.  For any other result, *MESSAGE and *USED
   are not specified.  Report an error as soon as the length and the
   type byte are present, before the rest of the frame.  */
extern enum rd_status rd_decode (const unsigned char *buf, size_t len,
				 struct rd_message *message, size_t *used);

#endif /* EMACS_RDPROTO_H */
