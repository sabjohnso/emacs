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

/* A text can have more bytes than one message holds, so the sender
   splits it into chunked messages (rdproto.h), and the receiver joins
   them.  A chunked message has the fields serial, offset, total and
   bytes.  The clipboard-data message is a chunked message.  The
   server (Emacs) and the client both use this file, as they use
   rdproto.c, so both sides have the same rules.  The file uses no
   Lisp objects.  */

#ifndef EMACS_RDCHUNK_H
#define EMACS_RDCHUNK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rdproto.h"

/* The fields of a chunked message, with no type: NBYTES bytes BYTES
   from OFFSET of the text SERIAL, which has TOTAL bytes.  BYTES points
   into the message.  */
struct rd_chunk_part
{
  uint32_t serial;
  uint32_t offset;
  uint32_t total;
  uint16_t nbytes;
  const uint8_t *bytes;
};

/* Set PART to the fields of MESSAGE, and return true.  Return false if
   MESSAGE is not a chunked message.  */
extern bool rd_chunk_part_of (const struct rd_message *message,
			      struct rd_chunk_part *part);

/* Return the number of messages of a text of SIZE bytes.  An empty
   text is one message with no bytes.  */
extern uint32_t rd_chunk_count (size_t size);

/* Set MESSAGE to the message INDEX of the type TYPE of the text SERIAL,
   which has the SIZE bytes TEXT.  TYPE is a chunked type.  INDEX is
   less than rd_chunk_count (SIZE), and SIZE fits in the total
   field.  */
extern void rd_chunk_message (enum rd_type type, uint32_t serial,
			      const unsigned char *text, size_t size,
			      uint32_t index, struct rd_message *message);

/* The state of a receiver: the text SERIAL that comes, if ACTIVE, with
   TOTAL bytes, of which RECEIVED bytes are in BYTES.  After a complete
   text, BYTES has the text until the next call of rd_chunks_add.
   Initialize the state with zeros, and free it with rd_chunks_free.  */
struct rd_chunks
{
  bool active;
  uint32_t serial;
  uint32_t total;
  uint32_t received;
  unsigned char *bytes;
};

/* The results of rd_chunks_add.  */
enum rd_chunk_result
  {
    /* The bytes are correct, and more bytes must come.  */
    RD_CHUNK_PARTIAL,
    /* The text is complete.  It is in the BYTES of the state, with
       TOTAL bytes.  */
    RD_CHUNK_DONE,
    /* The message does not continue a text, or the text has more bytes
       than the limit, or there is no memory.  The receiver drops the text.  */
    RD_CHUNK_REFUSED,
  };

/* Add the message part PART to the state CHUNKS.  A part at the offset
   0 with another serial than the text that comes starts a new text,
   and drops the old one.  A text with more than LIMIT bytes is
   refused.  */
extern enum rd_chunk_result rd_chunks_add
  (struct rd_chunks *chunks, const struct rd_chunk_part *part, size_t limit);

/* Free the memory of CHUNKS, and make it empty.  */
extern void rd_chunks_free (struct rd_chunks *chunks);

/* The queue of a sender of clipboard-data messages: the SIZE bytes
   of the text SERIAL that it sends, or BYTES NULL, and the index NEXT
   of the next message.  Only the functions below use the fields.  A
   sender sends one message at a time, at the pace of its connection,
   and a new text replaces the rest of the text before it.  Initialize
   the queue with zeros, and free it with rd_chunk_queue_clear.  */
struct rd_chunk_queue
{
  unsigned char *bytes;
  size_t size;
  uint32_t serial;
  uint32_t next;
};

/* Put the SIZE bytes TEXT in QUEUE with a new serial, and drop the rest
   of the text before it.  SIZE is at most RD_MAX_CLIPBOARD_SIZE.
   Return false if there is no memory; then QUEUE is empty.  */
extern bool rd_chunk_queue_set (struct rd_chunk_queue *queue,
				const unsigned char *text, size_t size);

/* Set MESSAGE to the next message of QUEUE, and return true.  Return
   false if QUEUE is empty.  After the last message of a text, QUEUE is
   empty.  */
extern bool rd_chunk_queue_next (struct rd_chunk_queue *queue,
				 struct rd_message *message);

/* Return true if QUEUE has a message to send.  */
extern bool rd_chunk_queue_pending_p (const struct rd_chunk_queue *queue);

/* Drop the text of QUEUE.  The serial stays.  */
extern void rd_chunk_queue_clear (struct rd_chunk_queue *queue);

#endif /* EMACS_RDCHUNK_H */
