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

   Each field is big-endian, and a signed field is two's complement.
   An array is its number of elements, 2 bytes, and then its elements.
   Positions and sizes of pixels have 32 bits, so the size of a frame
   does not limit them.  LENGTH is at most RD_MAX_LENGTH.

   A later version of the protocol can append fields to a message
   type.  A decoder reads the fields that it knows, and ignores the
   bytes that follow them.  A later version can also append message
   types.  A decoder reports RD_UNKNOWN_TYPE for such a frame, with its
   size, so the receiver can skip it.

   Each message type is one row of RD_MESSAGES and one list of fields.
   The enum rd_type value, the C struct, the wire layout, and the Lisp
   form all come from that one place.  */

#ifndef EMACS_RDPROTO_H
#define EMACS_RDPROTO_H

#include <stddef.h>
#include <stdint.h>

enum { RD_MAX_LENGTH = 1 << 24 };

/* The maximum number of glyphs in one draw-glyphs message.  The
   server sends a longer run of glyphs in more than one message.  */
enum { RD_MAX_GLYPHS = 512 };

/* The maximum number of rows of pixels in one draw-fringe message.
   The height of a fringe bitmap of Emacs has 8 bits.  */
enum { RD_MAX_FRINGE_ROWS = 255 };

/* The kinds of cursor.  A client ignores a kind that it does not
   know.  */
enum rd_cursor_kind
  {
    RD_CURSOR_BOX = 0,
    RD_CURSOR_HOLLOW_BOX = 1,
    RD_CURSOR_BAR = 2,
    RD_CURSOR_HBAR = 3,
  };

/* The message types.  A row is MESSAGE (ENUM, NAME, FIELDS): the name
   of the enum rd_type value is RD_ENUM, and the struct is struct
   rd_NAME.  FIELDS is the list of its fields.  The type byte of a row
   is its position, starting at 1, so a new type goes at the end.  */
#define RD_MESSAGES(MESSAGE)						\
  MESSAGE (HELLO, hello, RD_HELLO_FIELDS)				\
  MESSAGE (DRAW_GLYPHS, draw_glyphs, RD_DRAW_GLYPHS_FIELDS)		\
  MESSAGE (CLEAR_AREA, clear_area, RD_CLEAR_AREA_FIELDS)		\
  MESSAGE (COPY_AREA, copy_area, RD_COPY_AREA_FIELDS)			\
  MESSAGE (DRAW_CURSOR, draw_cursor, RD_DRAW_CURSOR_FIELDS)		\
  MESSAGE (DRAW_FRINGE, draw_fringe, RD_DRAW_FRINGE_FIELDS)

/* Each list gives the fields of a message in the order of the wire.
   FIELD (M, TYPE, NAME) is a member NAME of the C type TYPE_t.
   ARRAY (M, TYPE, NAME, COUNT, MAX) is the member COUNT, a uint16_t
   with the number of elements, and the array NAME of MAX elements.
   TYPE is one of uint8, uint16, uint32, and int32.  M is the name
   of the message, for the macro that reads the list.  */

/* The first message of each side of a connection.  */
#define RD_HELLO_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint16, version)

/* Draw glyphs of the frame font.  X and Y are the pixel position of
   the start of the baseline.  Each glyph code is a glyph index of
   the font, not a character.  A client changes only the pixels inside
   the rectangle at CLIP_X, CLIP_Y with the size CLIP_WIDTH,
   CLIP_HEIGHT, because a glyph can extend outside a line that is
   visible in part, or outside a text area.  A client draws only
   the glyphs, and not a background, because the glyphs of a composition
   overlap.  The server sends a clear-area message first.  */
#define RD_DRAW_GLYPHS_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, int32, clip_x)						\
  FIELD (M, int32, clip_y)						\
  FIELD (M, uint32, clip_width)						\
  FIELD (M, uint32, clip_height)					\
  ARRAY (M, uint32, glyphs, nglyphs, RD_MAX_GLYPHS)

/* Fill a rectangle with the background color of the frame.  X and Y
   are the pixel position of the top left corner.  */
#define RD_CLEAR_AREA_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint32, width)						\
  FIELD (M, uint32, height)

/* Copy the rectangle at X, Y with the size WIDTH, HEIGHT to the
   position TO_X, TO_Y of the same frame.  The rectangles can
   overlap.  */
#define RD_COPY_AREA_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint32, width)						\
  FIELD (M, uint32, height)						\
  FIELD (M, int32, to_x)						\
  FIELD (M, int32, to_y)

/* Draw a cursor of the rd_cursor_kind KIND in the rectangle at X, Y
   with the size WIDTH, HEIGHT.  The server erases a cursor: it clears
   the area under the cursor, and draws its glyphs again.  */
#define RD_DRAW_CURSOR_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint32, width)						\
  FIELD (M, uint32, height)						\
  FIELD (M, uint8, kind)

/* Draw a fringe bitmap at X, Y.  The message holds the rows of the
   bitmap, so a client needs no table of bitmaps when it connects
   again.  In each element of ROWS, the bits from 0 to WIDTH - 1 are
   the pixels, and bit 0 is the rightmost pixel, so WIDTH has 16 bits
   and not the 32 bits of the other sizes.  If OVERLAY is not
   zero, draw on the pixels that are there.  If CURSOR is not zero,
   the bitmap is the cursor.  */
#define RD_DRAW_FRINGE_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint16, width)						\
  FIELD (M, uint8, overlay)						\
  FIELD (M, uint8, cursor)						\
  ARRAY (M, uint16, rows, nrows, RD_MAX_FRINGE_ROWS)

/* The value 0 is not a message type.  */
#define RD_TYPE_ENUMERATOR(ENUM, NAME, FIELDS) RD_##ENUM,
enum rd_type
  {
    RD_TYPE_NONE,
    RD_MESSAGES (RD_TYPE_ENUMERATOR)
    /* One more than the last message type.  */
    RD_TYPE_LIMIT
  };
#undef RD_TYPE_ENUMERATOR

#define RD_MEMBER(M, TYPE, NAME) TYPE##_t NAME;
#define RD_ARRAY_MEMBER(M, TYPE, NAME, COUNT, MAX)	\
  uint16_t COUNT;					\
  TYPE##_t NAME[MAX];
#define RD_STRUCT(ENUM, NAME, FIELDS)				\
  struct rd_##NAME { FIELDS (RD_MEMBER, RD_ARRAY_MEMBER, NAME) };
RD_MESSAGES (RD_STRUCT)
#undef RD_STRUCT
#undef RD_ARRAY_MEMBER
#undef RD_MEMBER

struct rd_message
{
  enum rd_type type;
  union
  {
#define RD_UNION_MEMBER(ENUM, NAME, FIELDS) struct rd_##NAME NAME;
    RD_MESSAGES (RD_UNION_MEMBER)
#undef RD_UNION_MEMBER
  } u;
};

/* The form of a value of a field.  SIZE is its size in a struct and on
   the wire.  MIN and MAX are its limits.  */
struct rd_value_type
{
  size_t size;
  int64_t min;
  int64_t max;
  int64_t (*load) (const void *place);
  void (*store) (void *place, int64_t value);
};

/* One field of a message type.  NAME is the name of the member.
   OFFSET is its position in struct rd_message.  An array has MAX_COUNT
   above 0, and COUNT_OFFSET is the position of its number of
   elements.  */
struct rd_field
{
  const char *name;
  const struct rd_value_type *value;
  size_t offset;
  size_t count_offset;
  int max_count;
};

/* One message type: the name of its struct, and its NFIELDS fields in
   the order of the wire.  */
struct rd_format
{
  const char *name;
  const struct rd_field *fields;
  int nfields;
};

/* The message types, indexed by enum rd_type from RD_HELLO.  */
extern const struct rd_format rd_formats[RD_TYPE_LIMIT];

/* Return the number of elements of the array F of MESSAGE, and set
   that number to COUNT, which must be at most the MAX_COUNT of F.  */
extern int rd_array_count (const struct rd_message *message,
			   const struct rd_field *f);
extern void rd_set_array_count (struct rd_message *message,
				const struct rd_field *f, int count);

/* Return the element INDEX of the field F of MESSAGE, and set it to
   VALUE, which must be in the limits of F.  INDEX is 0 for a field
   that is not an array.  */
extern int64_t rd_field_value (const struct rd_message *message,
			       const struct rd_field *f, int index);
extern void rd_set_field_value (struct rd_message *message,
				const struct rd_field *f, int index,
				int64_t value);

enum rd_status
  {
    /* BUF holds a complete message.  */
    RD_OK,
    /* BUF holds only the start of a message.  Read more bytes.  */
    RD_INCOMPLETE,
    /* The type byte is not an enum rd_type value.  The frame can be
       from a later version of the protocol.  */
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
   bytes that it occupies.  If the result is RD_UNKNOWN_TYPE, set *USED
   to the size of the frame, so a receiver can skip it.  That size can
   be more than LEN, and the receiver must then wait for the other
   bytes.  For any other result, *MESSAGE and *USED are not specified.
   Report an error as soon as the length and the type byte are present,
   before the rest of the frame.  */
extern enum rd_status rd_decode (const unsigned char *buf, size_t len,
				 struct rd_message *message, size_t *used);

#endif /* EMACS_RDPROTO_H */
