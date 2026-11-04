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

static const struct rd_value_type uint8_value;
static const struct rd_value_type uint16_value;
static const struct rd_value_type uint32_value;
static const struct rd_value_type int32_value;

/* The fields of each message type.  */
#define FIELD_ROW(M, TYPE, NAME)					\
  { #NAME, &TYPE##_value, offsetof (struct rd_message, u.M.NAME), 0, 0 },
#define ARRAY_ROW(M, TYPE, NAME, COUNT, MAX)				\
  { #NAME, &TYPE##_value, offsetof (struct rd_message, u.M.NAME),	\
    offsetof (struct rd_message, u.M.COUNT), MAX },
#define FIELD_TABLE(ENUM, NAME, FIELDS)					\
  static const struct rd_field NAME##_fields[] =			\
    { FIELDS (FIELD_ROW, ARRAY_ROW, NAME) };
RD_MESSAGES (FIELD_TABLE)

#define FORMAT_ROW(ENUM, NAME, FIELDS)					\
  [RD_##ENUM] = { #NAME, NAME##_fields,					\
		  sizeof NAME##_fields / sizeof *NAME##_fields },
const struct rd_format rd_formats[RD_TYPE_LIMIT] =
  {
    RD_MESSAGES (FORMAT_ROW)
  };

static size_t fixed_size (const struct rd_format *);
static const struct rd_format *format_of_byte (unsigned int);
static unsigned char *put_field (unsigned char *, const struct rd_field *,
				 const struct rd_message *);
static enum rd_status get_fields (const struct rd_format *,
				  const unsigned char *, size_t,
				  struct rd_message *);
static unsigned char *put_value (unsigned char *, const struct rd_value_type *,
				 int64_t);
static int64_t get_value (const unsigned char *,
			  const struct rd_value_type *);
static const void *field_place (const struct rd_message *,
				const struct rd_field *, int);

size_t
rd_encoded_size (const struct rd_message *message)
{
  const struct rd_format *format = &rd_formats[message->type];
  size_t size = LENGTH_SIZE + fixed_size (format);
  for (int i = 0; i < format->nfields; i++)
    {
      const struct rd_field *f = &format->fields[i];
      if (f->max_count)
	size += rd_array_count (message, f) * f->value->size;
    }
  return size;
}

void
rd_encode (const struct rd_message *message, unsigned char *buf)
{
  const struct rd_format *format = &rd_formats[message->type];
  unsigned char *p = put_value (buf, &uint32_value,
				rd_encoded_size (message) - LENGTH_SIZE);
  p = put_value (p, &uint8_value, message->type);
  for (int i = 0; i < format->nfields; i++)
    p = put_field (p, &format->fields[i], message);
}

enum rd_status
rd_decode (const unsigned char *buf, size_t len,
	   struct rd_message *message, size_t *used)
{
  if (len < LENGTH_SIZE)
    return RD_INCOMPLETE;
  uint32_t length = get_value (buf, &uint32_value);
  if (length > RD_MAX_LENGTH)
    return RD_FRAME_TOO_LONG;
  if (length < 1)
    return RD_BAD_LENGTH;
  if (len == LENGTH_SIZE)
    return RD_INCOMPLETE;

  unsigned int type = buf[LENGTH_SIZE];
  const struct rd_format *format = format_of_byte (type);
  if (!format)
    {
      *used = LENGTH_SIZE + length;
      return RD_UNKNOWN_TYPE;
    }
  if (length < fixed_size (format))
    return RD_BAD_LENGTH;
  if (len - LENGTH_SIZE < length)
    return RD_INCOMPLETE;

  message->type = type;
  enum rd_status status = get_fields (format, buf + LENGTH_SIZE + 1,
				      length - 1, message);
  if (status == RD_OK)
    *used = LENGTH_SIZE + length;
  return status;
}

int
rd_array_count (const struct rd_message *message, const struct rd_field *f)
{
  return *(const uint16_t *) ((const char *) message + f->count_offset);
}

void
rd_set_array_count (struct rd_message *message, const struct rd_field *f,
		    int count)
{
  *(uint16_t *) ((char *) message + f->count_offset) = count;
}

int64_t
rd_field_value (const struct rd_message *message, const struct rd_field *f,
		int index)
{
  return f->value->load (field_place (message, f, index));
}

void
rd_set_field_value (struct rd_message *message, const struct rd_field *f,
		    int index, int64_t value)
{
  f->value->store ((void *) field_place (message, f, index), value);
}

/* Return the size of the type byte and the bytes that each message of
   FORMAT has.  The elements of an array are not in this size, but the
   number of elements is.  */
static size_t
fixed_size (const struct rd_format *format)
{
  size_t size = 1;
  for (int i = 0; i < format->nfields; i++)
    {
      const struct rd_field *f = &format->fields[i];
      size += f->max_count ? uint16_value.size : f->value->size;
    }
  return size;
}

/* Return the format of the type byte TYPE, or NULL if TYPE is not a
   message type.  */
static const struct rd_format *
format_of_byte (unsigned int type)
{
  if (RD_TYPE_NONE < type && type < RD_TYPE_LIMIT)
    return &rd_formats[type];
  return NULL;
}

/* Write the field F of MESSAGE at P, and return the end of it.  */
static unsigned char *
put_field (unsigned char *p, const struct rd_field *f,
	   const struct rd_message *message)
{
  if (!f->max_count)
    return put_value (p, f->value, rd_field_value (message, f, 0));
  int count = rd_array_count (message, f);
  p = put_value (p, &uint16_value, count);
  for (int i = 0; i < count; i++)
    p = put_value (p, f->value, rd_field_value (message, f, i));
  return p;
}

/* Read the fields of FORMAT from the SIZE bytes at P, which hold at
   least the bytes of `fixed_size'.  Return RD_OK or RD_BAD_LENGTH.  The
   bytes after the fixed bytes are the only place for the elements of an
   array.  */
static enum rd_status
get_fields (const struct rd_format *format, const unsigned char *p,
	    size_t size, struct rd_message *message)
{
  size_t spare = size + 1 - fixed_size (format);
  for (int i = 0; i < format->nfields; i++)
    {
      const struct rd_field *f = &format->fields[i];
      if (!f->max_count)
	{
	  rd_set_field_value (message, f, 0, get_value (p, f->value));
	  p += f->value->size;
	  continue;
	}
      int count = get_value (p, &uint16_value);
      p += uint16_value.size;
      if (count > f->max_count || count * f->value->size > spare)
	return RD_BAD_LENGTH;
      spare -= count * f->value->size;
      rd_set_array_count (message, f, count);
      for (int element = 0; element < count; element++)
	{
	  rd_set_field_value (message, f, element, get_value (p, f->value));
	  p += f->value->size;
	}
    }
  return RD_OK;
}

/* Write VALUE at P in the big-endian form of TYPE, and return the end
   of it.  The conversion to uint64_t is modulo 2^64, so a negative
   value has its two's complement bits.  */
static unsigned char *
put_value (unsigned char *p, const struct rd_value_type *type, int64_t value)
{
  uint64_t bits = value;
  for (size_t i = type->size; i > 0; i--)
    {
      p[i - 1] = bits & 0xff;
      bits >>= 8;
    }
  return p + type->size;
}

/* Read the big-endian value of TYPE at P.  A value above the limit of
   TYPE is the bit pattern of a negative value.  */
static int64_t
get_value (const unsigned char *p, const struct rd_value_type *type)
{
  int64_t bits = 0;
  for (size_t i = 0; i < type->size; i++)
    bits = (bits << 8) | p[i];
  return bits <= type->max ? bits : bits - (type->max - type->min + 1);
}

/* Return the place of the element INDEX of F in MESSAGE.  */
static const void *
field_place (const struct rd_message *message, const struct rd_field *f,
	     int index)
{
  return (const char *) message + f->offset + index * f->value->size;
}

/* Define the load and store functions of the C type TYPE##_t, and its
   row of the value types.  */
#define VALUE_TYPE(TYPE, MIN, MAX)					\
  static int64_t							\
  load_##TYPE (const void *place)					\
  {									\
    return *(const TYPE##_t *) place;					\
  }									\
									\
  static void								\
  store_##TYPE (void *place, int64_t value)				\
  {									\
    *(TYPE##_t *) place = value;					\
  }									\
									\
  static const struct rd_value_type TYPE##_value =			\
    { sizeof (TYPE##_t), MIN, MAX, load_##TYPE, store_##TYPE }

VALUE_TYPE (uint8, 0, UINT8_MAX);
VALUE_TYPE (uint16, 0, UINT16_MAX);
VALUE_TYPE (uint32, 0, UINT32_MAX);
VALUE_TYPE (int32, INT32_MIN, INT32_MAX);
