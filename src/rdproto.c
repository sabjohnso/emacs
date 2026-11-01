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

/* The codec of one message type.  FIXED_SIZE is the size of the
   type byte and the fields that each message of the type has.
   EXTRA_SIZE gives the size of the fields that follow them, for
   example an array.  PUT writes the fields after the type byte.  GET
   reads the SIZE bytes of fields at FIELDS, and returns RD_OK or
   RD_BAD_LENGTH.  */
struct codec
{
  size_t fixed_size;
  size_t (*extra_size) (const struct rd_message *);
  void (*put) (const struct rd_message *, unsigned char *);
  enum rd_status (*get) (const unsigned char *, size_t,
			 struct rd_message *);
};

static const struct codec *message_codec (const struct rd_message *);
static const struct codec *codec_of (unsigned int);
static size_t no_extra_size (const struct rd_message *);
static void put_hello (const struct rd_message *, unsigned char *);
static enum rd_status get_hello (const unsigned char *, size_t,
				 struct rd_message *);
static size_t draw_glyphs_extra_size (const struct rd_message *);
static void put_draw_glyphs (const struct rd_message *, unsigned char *);
static enum rd_status get_draw_glyphs (const unsigned char *, size_t,
				       struct rd_message *);
static void put_clear_area (const struct rd_message *, unsigned char *);
static enum rd_status get_clear_area (const unsigned char *, size_t,
				      struct rd_message *);
static void put_copy_area (const struct rd_message *, unsigned char *);
static enum rd_status get_copy_area (const unsigned char *, size_t,
				     struct rd_message *);
static void put_uint16 (unsigned char *, uint16_t);
static void put_uint32 (unsigned char *, uint32_t);
static uint16_t get_uint16 (const unsigned char *);
static uint32_t get_uint32 (const unsigned char *);

/* The offsets of the fields of a draw-glyphs message.  The glyph
   codes follow the number of glyphs, GLYPH_SIZE bytes each.  */
enum
  {
    DRAW_GLYPHS_FRAME = 0,
    DRAW_GLYPHS_X = DRAW_GLYPHS_FRAME + 4,
    DRAW_GLYPHS_Y = DRAW_GLYPHS_X + 2,
    DRAW_GLYPHS_COUNT = DRAW_GLYPHS_Y + 2,
    DRAW_GLYPHS_HEAD = DRAW_GLYPHS_COUNT + 2,
    GLYPH_SIZE = 4,
  };

/* The offsets of the fields of a clear-area message.  */
enum
  {
    CLEAR_AREA_FRAME = 0,
    CLEAR_AREA_X = CLEAR_AREA_FRAME + 4,
    CLEAR_AREA_Y = CLEAR_AREA_X + 2,
    CLEAR_AREA_WIDTH = CLEAR_AREA_Y + 2,
    CLEAR_AREA_HEIGHT = CLEAR_AREA_WIDTH + 2,
    CLEAR_AREA_SIZE = CLEAR_AREA_HEIGHT + 2,
  };

/* The offsets of the fields of a copy-area message.  */
enum
  {
    COPY_AREA_FRAME = 0,
    COPY_AREA_X = COPY_AREA_FRAME + 4,
    COPY_AREA_Y = COPY_AREA_X + 2,
    COPY_AREA_WIDTH = COPY_AREA_Y + 2,
    COPY_AREA_HEIGHT = COPY_AREA_WIDTH + 2,
    COPY_AREA_TO_X = COPY_AREA_HEIGHT + 2,
    COPY_AREA_TO_Y = COPY_AREA_TO_X + 2,
    COPY_AREA_SIZE = COPY_AREA_TO_Y + 2,
  };

/* The codecs, indexed by enum rd_type.  */
static const struct codec codecs[] =
  {
    [RD_HELLO] = { 1 + 2, no_extra_size, put_hello, get_hello },
    [RD_DRAW_GLYPHS] = { 1 + DRAW_GLYPHS_HEAD, draw_glyphs_extra_size,
			 put_draw_glyphs, get_draw_glyphs },
    [RD_CLEAR_AREA] = { 1 + CLEAR_AREA_SIZE, no_extra_size,
			put_clear_area, get_clear_area },
    [RD_COPY_AREA] = { 1 + COPY_AREA_SIZE, no_extra_size,
		       put_copy_area, get_copy_area },
  };

size_t
rd_encoded_size (const struct rd_message *message)
{
  const struct codec *codec = message_codec (message);
  return LENGTH_SIZE + codec->fixed_size + codec->extra_size (message);
}

void
rd_encode (const struct rd_message *message, unsigned char *buf)
{
  put_uint32 (buf, rd_encoded_size (message) - LENGTH_SIZE);
  buf[LENGTH_SIZE] = message->type;
  message_codec (message)->put (message, buf + LENGTH_SIZE + 1);
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
  const struct codec *codec = codec_of (type);
  if (!codec)
    return RD_UNKNOWN_TYPE;
  if (length < codec->fixed_size)
    return RD_BAD_LENGTH;
  if (len - LENGTH_SIZE < length)
    return RD_INCOMPLETE;

  message->type = type;
  enum rd_status status = codec->get (buf + LENGTH_SIZE + 1, length - 1,
				      message);
  if (status == RD_OK)
    *used = LENGTH_SIZE + length;
  return status;
}

/* Return the codec of MESSAGE.  The type of MESSAGE must be valid.  */
static const struct codec *
message_codec (const struct rd_message *message)
{
  const struct codec *codec = codec_of (message->type);
  if (!codec)
    abort ();
  return codec;
}

/* Return the codec of TYPE, or NULL if TYPE is not a message type.  */
static const struct codec *
codec_of (unsigned int type)
{
  if (type < sizeof codecs / sizeof *codecs && codecs[type].put)
    return &codecs[type];
  return NULL;
}

static size_t
no_extra_size (const struct rd_message *message)
{
  return 0;
}

static void
put_hello (const struct rd_message *message, unsigned char *fields)
{
  put_uint16 (fields, message->u.hello.version);
}

static enum rd_status
get_hello (const unsigned char *fields, size_t size,
	   struct rd_message *message)
{
  message->u.hello.version = get_uint16 (fields);
  return RD_OK;
}

static size_t
draw_glyphs_extra_size (const struct rd_message *message)
{
  return GLYPH_SIZE * message->u.draw_glyphs.nglyphs;
}

static void
put_draw_glyphs (const struct rd_message *message, unsigned char *fields)
{
  const struct rd_draw_glyphs *m = &message->u.draw_glyphs;
  put_uint32 (fields + DRAW_GLYPHS_FRAME, m->frame);
  put_uint16 (fields + DRAW_GLYPHS_X, m->x);
  put_uint16 (fields + DRAW_GLYPHS_Y, m->y);
  put_uint16 (fields + DRAW_GLYPHS_COUNT, m->nglyphs);
  for (int i = 0; i < m->nglyphs; i++)
    put_uint32 (fields + DRAW_GLYPHS_HEAD + GLYPH_SIZE * i, m->glyphs[i]);
}

static enum rd_status
get_draw_glyphs (const unsigned char *fields, size_t size,
		 struct rd_message *message)
{
  struct rd_draw_glyphs *m = &message->u.draw_glyphs;
  m->nglyphs = get_uint16 (fields + DRAW_GLYPHS_COUNT);
  if (m->nglyphs > RD_MAX_GLYPHS
      || size < DRAW_GLYPHS_HEAD + GLYPH_SIZE * m->nglyphs)
    return RD_BAD_LENGTH;
  m->frame = get_uint32 (fields + DRAW_GLYPHS_FRAME);
  m->x = get_uint16 (fields + DRAW_GLYPHS_X);
  m->y = get_uint16 (fields + DRAW_GLYPHS_Y);
  for (int i = 0; i < m->nglyphs; i++)
    m->glyphs[i] = get_uint32 (fields + DRAW_GLYPHS_HEAD + GLYPH_SIZE * i);
  return RD_OK;
}

static void
put_clear_area (const struct rd_message *message, unsigned char *fields)
{
  const struct rd_clear_area *m = &message->u.clear_area;
  put_uint32 (fields + CLEAR_AREA_FRAME, m->frame);
  put_uint16 (fields + CLEAR_AREA_X, m->x);
  put_uint16 (fields + CLEAR_AREA_Y, m->y);
  put_uint16 (fields + CLEAR_AREA_WIDTH, m->width);
  put_uint16 (fields + CLEAR_AREA_HEIGHT, m->height);
}

static enum rd_status
get_clear_area (const unsigned char *fields, size_t size,
		struct rd_message *message)
{
  struct rd_clear_area *m = &message->u.clear_area;
  m->frame = get_uint32 (fields + CLEAR_AREA_FRAME);
  m->x = get_uint16 (fields + CLEAR_AREA_X);
  m->y = get_uint16 (fields + CLEAR_AREA_Y);
  m->width = get_uint16 (fields + CLEAR_AREA_WIDTH);
  m->height = get_uint16 (fields + CLEAR_AREA_HEIGHT);
  return RD_OK;
}

static void
put_copy_area (const struct rd_message *message, unsigned char *fields)
{
  const struct rd_copy_area *m = &message->u.copy_area;
  put_uint32 (fields + COPY_AREA_FRAME, m->frame);
  put_uint16 (fields + COPY_AREA_X, m->x);
  put_uint16 (fields + COPY_AREA_Y, m->y);
  put_uint16 (fields + COPY_AREA_WIDTH, m->width);
  put_uint16 (fields + COPY_AREA_HEIGHT, m->height);
  put_uint16 (fields + COPY_AREA_TO_X, m->to_x);
  put_uint16 (fields + COPY_AREA_TO_Y, m->to_y);
}

static enum rd_status
get_copy_area (const unsigned char *fields, size_t size,
	       struct rd_message *message)
{
  struct rd_copy_area *m = &message->u.copy_area;
  m->frame = get_uint32 (fields + COPY_AREA_FRAME);
  m->x = get_uint16 (fields + COPY_AREA_X);
  m->y = get_uint16 (fields + COPY_AREA_Y);
  m->width = get_uint16 (fields + COPY_AREA_WIDTH);
  m->height = get_uint16 (fields + COPY_AREA_HEIGHT);
  m->to_x = get_uint16 (fields + COPY_AREA_TO_X);
  m->to_y = get_uint16 (fields + COPY_AREA_TO_Y);
  return RD_OK;
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
