/* Split a text into messages, and join the messages again.

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
#include <stdlib.h>
#include <string.h>

#include "rdchunk.h"

/* rd_chunk_count and rd_chunk_message use one chunk size for each
   chunked type.  */
static_assert ((int) RD_MAX_IMAGE_CHUNK == (int) RD_MAX_CLIPBOARD_CHUNK,
	       "the chunked types have one chunk size");

static bool continues_p (const struct rd_chunks *,
			 const struct rd_chunk_part *);
static bool start (struct rd_chunks *, const struct rd_chunk_part *, size_t);
static void set_part (struct rd_message *, const struct rd_chunk_part *);

/* The part of the struct DATA of a chunked message (RD_CHUNKED_MESSAGES
   in rdproto.h), with the field SERIAL.  */
#define PART_OF(DATA, SERIAL)						\
  ((struct rd_chunk_part) { .serial = (DATA)->SERIAL,			\
			    .offset = (DATA)->offset,			\
			    .total = (DATA)->total,			\
			    .nbytes = (DATA)->nbytes,			\
			    .bytes = (DATA)->bytes })

/* Copy the fields of the part PART to the struct DATA of a chunked
   message, with the field SERIAL.  */
#define SET_PART(DATA, SERIAL, PART)					\
  do									\
    {									\
      (DATA)->SERIAL = (PART)->serial;					\
      (DATA)->offset = (PART)->offset;					\
      (DATA)->total = (PART)->total;					\
      (DATA)->nbytes = (PART)->nbytes;					\
      if ((PART)->nbytes)						\
	memcpy ((DATA)->bytes, (PART)->bytes, (PART)->nbytes);		\
    }									\
  while (false)

bool
rd_chunk_part_of (const struct rd_message *message,
		  struct rd_chunk_part *part)
{
  switch (message->type)
    {
#define PART_OF_CASE(ENUM, NAME, SERIAL)			\
    case RD_##ENUM:						\
      *part = PART_OF (&message->u.NAME, SERIAL);		\
      return true;
      RD_CHUNKED_MESSAGES (PART_OF_CASE)
#undef PART_OF_CASE
    default:
      return false;
    }
}

uint32_t
rd_chunk_count (size_t size)
{
  return size == 0 ? 1
    : (size + RD_MAX_CLIPBOARD_CHUNK - 1) / RD_MAX_CLIPBOARD_CHUNK;
}

void
rd_chunk_message (enum rd_type type, uint32_t serial,
		  const unsigned char *text, size_t size, uint32_t index,
		  struct rd_message *message)
{
  size_t offset = (size_t) index * RD_MAX_CLIPBOARD_CHUNK;
  size_t left = size - offset;
  struct rd_chunk_part part
    = { .serial = serial, .offset = offset, .total = size,
	.nbytes = (left < RD_MAX_CLIPBOARD_CHUNK ? left
		   : RD_MAX_CLIPBOARD_CHUNK),
	.bytes = text + offset };
  *message = (struct rd_message) { .type = type };
  set_part (message, &part);
}

enum rd_chunk_result
rd_chunks_add (struct rd_chunks *chunks, const struct rd_chunk_part *data,
	       size_t limit)
{
  if (data->offset == 0
      && !(chunks->active && chunks->serial == data->serial)
      && !start (chunks, data, limit))
    return RD_CHUNK_REFUSED;
  if (!continues_p (chunks, data))
    {
      chunks->active = false;
      return RD_CHUNK_REFUSED;
    }
  if (data->nbytes)
    memcpy (chunks->bytes + chunks->received, data->bytes, data->nbytes);
  chunks->received += data->nbytes;
  if (chunks->received < chunks->total)
    return RD_CHUNK_PARTIAL;
  chunks->active = false;
  return RD_CHUNK_DONE;
}

enum rd_chunk_result
rd_chunks_add_message (struct rd_chunks *chunks,
		       const struct rd_message *message, size_t limit)
{
  struct rd_chunk_part part;
  return (rd_chunk_part_of (message, &part)
	  ? rd_chunks_add (chunks, &part, limit) : RD_CHUNK_REFUSED);
}

void
rd_chunks_free (struct rd_chunks *chunks)
{
  free (chunks->bytes);
  *chunks = (struct rd_chunks) { 0 };
}

bool
rd_chunk_queue_set (struct rd_chunk_queue *queue, const unsigned char *text,
		    size_t size)
{
  rd_chunk_queue_clear (queue);
  queue->bytes = malloc (size ? size : 1);
  if (!queue->bytes)
    return false;
  if (size)
    memcpy (queue->bytes, text, size);
  queue->size = size;
  queue->serial++;
  return true;
}

bool
rd_chunk_queue_next (struct rd_chunk_queue *queue, struct rd_message *message)
{
  if (!rd_chunk_queue_pending_p (queue))
    return false;
  rd_chunk_message (RD_CLIPBOARD_DATA, queue->serial, queue->bytes,
		    queue->size, queue->next++, message);
  if (queue->next == rd_chunk_count (queue->size))
    rd_chunk_queue_clear (queue);
  return true;
}

bool
rd_chunk_queue_pending_p (const struct rd_chunk_queue *queue)
{
  return queue->bytes != NULL;
}

void
rd_chunk_queue_clear (struct rd_chunk_queue *queue)
{
  free (queue->bytes);
  queue->bytes = NULL;
  queue->size = 0;
  queue->next = 0;
}

/* Return true if DATA is the next part of the text of CHUNKS, and ends
   at or before the end of the text.  */
static bool
continues_p (const struct rd_chunks *chunks,
	     const struct rd_chunk_part *data)
{
  return (chunks->active && data->serial == chunks->serial
	  && data->offset == chunks->received
	  && data->total == chunks->total
	  && data->nbytes <= chunks->total - chunks->received);
}

/* Start the text of DATA in CHUNKS, and drop the text before it.
   Return false if the text has more than LIMIT bytes, or if there is
   no memory.  */
static bool
start (struct rd_chunks *chunks, const struct rd_chunk_part *data,
       size_t limit)
{
  chunks->active = false;
  if (data->total > limit)
    return false;
  unsigned char *bytes = realloc (chunks->bytes,
				  data->total ? data->total : 1);
  if (!bytes)
    return false;
  *chunks = (struct rd_chunks) { .active = true, .serial = data->serial,
				 .total = data->total, .bytes = bytes };
  return true;
}

/* Copy the fields of PART to MESSAGE, which has a chunked type.  */
static void
set_part (struct rd_message *message, const struct rd_chunk_part *part)
{
  switch (message->type)
    {
#define SET_PART_CASE(ENUM, NAME, SERIAL)			\
    case RD_##ENUM:						\
      SET_PART (&message->u.NAME, SERIAL, part);		\
      break;
      RD_CHUNKED_MESSAGES (SET_PART_CASE)
#undef SET_PART_CASE
    default:
      /* The caller gives only a chunked type (rdchunk.h).  The server
	 build defines NDEBUG, so an assertion would do nothing there.  */
      abort ();
      break;
    }
}
