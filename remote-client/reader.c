/* The reader module of the remote display client.

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

/* The reader keeps the bytes of the message that is not complete.
   For a frame of an unknown type that is not complete, it keeps only
   the number of bytes to skip.  The client cannot continue with no
   memory, so a failed allocation stops the program.  */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include "reader.h"

struct rc_reader
{
  rc_message_function *receive;
  void *data;
  /* The USED bytes of BUFFER are the start of a frame.  BUFFER has
     SIZE bytes.  */
  unsigned char *buffer;
  size_t used;
  size_t size;
  /* The number of bytes of an unknown frame that did not come.  */
  size_t skip;
  const char *error;
};

static size_t skip_bytes (struct rc_reader *, size_t);
static void add_bytes (struct rc_reader *, const unsigned char *, size_t);
static void decode_messages (struct rc_reader *);
static const char *status_error (enum rd_status);

struct rc_reader *
rc_reader_new (rc_message_function *receive, void *data)
{
  struct rc_reader *reader = calloc (1, sizeof *reader);
  if (!reader)
    abort ();
  reader->receive = receive;
  reader->data = data;
  return reader;
}

void
rc_reader_free (struct rc_reader *reader)
{
  free (reader->buffer);
  free (reader);
}

bool
rc_reader_feed (struct rc_reader *reader, const unsigned char *bytes,
		size_t size)
{
  if (reader->error)
    return false;
  size_t skipped = skip_bytes (reader, size);
  add_bytes (reader, bytes + skipped, size - skipped);
  decode_messages (reader);
  return !reader->error;
}

const char *
rc_reader_error (const struct rc_reader *reader)
{
  return reader->error;
}

/* Return the number of the next SIZE bytes that are part of an
   unknown frame, and count them as skipped.  */
static size_t
skip_bytes (struct rc_reader *reader, size_t size)
{
  size_t skipped = size < reader->skip ? size : reader->skip;
  reader->skip -= skipped;
  return skipped;
}

/* Add the SIZE BYTES to the buffer of READER.  With no bytes, the
   buffer can be NULL, and memcpy must not get it.  */
static void
add_bytes (struct rc_reader *reader, const unsigned char *bytes,
	   size_t size)
{
  if (size == 0)
    return;
  if (reader->size - reader->used < size)
    {
      reader->size = 2 * (reader->used + size);
      reader->buffer = realloc (reader->buffer, reader->size);
      if (!reader->buffer)
	abort ();
    }
  memcpy (reader->buffer + reader->used, bytes, size);
  reader->used += size;
}

/* Give each complete message of the buffer of READER to its function,
   and keep only the bytes after them.  */
static void
decode_messages (struct rc_reader *reader)
{
  size_t start = 0;
  while (start < reader->used)
    {
      struct rd_message message;
      size_t used;
      size_t left = reader->used - start;
      enum rd_status status = rd_decode (reader->buffer + start, left,
					 &message, &used);
      if (status == RD_INCOMPLETE)
	break;
      if (status != RD_OK && status != RD_UNKNOWN_TYPE)
	{
	  /* USED has no value.  The reader ignores the bytes after an
	     error, so it does not need to keep them.  */
	  reader->error = status_error (status);
	  return;
	}
      if (status == RD_OK)
	reader->receive (reader->data, &message);
      else if (used > left)
	{
	  reader->skip = used - left;
	  used = left;
	}
      start += used;
    }
  if (start == 0)
    return;
  memmove (reader->buffer, reader->buffer + start, reader->used - start);
  reader->used -= start;
}

/* Return the description of the error STATUS of rd_decode.  */
static const char *
status_error (enum rd_status status)
{
  switch (status)
    {
    case RD_BAD_LENGTH:
      return "A frame has a length that is too small for its type";
    case RD_FRAME_TOO_LONG:
      return "A frame is longer than the maximum length";
    default:
      return "The decoder returned an unknown status";
    }
}
