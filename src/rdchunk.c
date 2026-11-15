/* Split a clipboard text into messages, and join the messages again.

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
#include <string.h>

#include "rdchunk.h"

static bool continues_p (const struct rd_chunks *,
			 const struct rd_clipboard_data *);
static bool start (struct rd_chunks *, const struct rd_clipboard_data *);

uint32_t
rd_chunk_count (size_t size)
{
  return size == 0 ? 1
    : (size + RD_MAX_CLIPBOARD_CHUNK - 1) / RD_MAX_CLIPBOARD_CHUNK;
}

void
rd_chunk_message (uint32_t serial, const unsigned char *text, size_t size,
		  uint32_t index, struct rd_message *message)
{
  size_t offset = (size_t) index * RD_MAX_CLIPBOARD_CHUNK;
  size_t left = size - offset;
  *message = (struct rd_message) { .type = RD_CLIPBOARD_DATA };
  struct rd_clipboard_data *data = &message->u.clipboard_data;
  data->serial = serial;
  data->offset = offset;
  data->total = size;
  data->nbytes = left < RD_MAX_CLIPBOARD_CHUNK ? left
    : RD_MAX_CLIPBOARD_CHUNK;
  if (data->nbytes)
    memcpy (data->bytes, text + offset, data->nbytes);
}

enum rd_chunk_result
rd_chunks_add (struct rd_chunks *chunks,
	       const struct rd_clipboard_data *data)
{
  if (data->offset == 0
      && !(chunks->active && chunks->serial == data->serial)
      && !start (chunks, data))
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
  rd_chunk_message (queue->serial, queue->bytes, queue->size, queue->next++,
		    message);
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
	     const struct rd_clipboard_data *data)
{
  return (chunks->active && data->serial == chunks->serial
	  && data->offset == chunks->received
	  && data->total == chunks->total
	  && data->nbytes <= chunks->total - chunks->received);
}

/* Start the text of DATA in CHUNKS, and drop the text before it.
   Return false if the text is too large, or if there is no memory.  */
static bool
start (struct rd_chunks *chunks, const struct rd_clipboard_data *data)
{
  chunks->active = false;
  if (data->total > RD_MAX_CLIPBOARD_SIZE)
    return false;
  unsigned char *bytes = realloc (chunks->bytes,
				  data->total ? data->total : 1);
  if (!bytes)
    return false;
  *chunks = (struct rd_chunks) { .active = true, .serial = data->serial,
				 .total = data->total, .bytes = bytes };
  return true;
}
