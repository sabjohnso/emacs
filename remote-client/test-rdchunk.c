/* Test the split and the join of chunked texts.

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

/* The laws of ../src/rdchunk.c, for random texts.  Some texts are
   empty, and some have a size near a multiple of
   RD_MAX_CLIPBOARD_CHUNK.

   - Split: rd_chunk_count gives 1 for an empty text, and else the
     number of chunks of RD_MAX_CLIPBOARD_CHUNK bytes.  The messages of
     a text have the serial and the total of the text, and the offset
     of each one is the sum of the sizes before it.  Each message but
     the last has RD_MAX_CLIPBOARD_CHUNK bytes, and the last one has at
     least one byte if the text is not empty.
   - Round trip: the messages of a text, in order, give
     RD_CHUNK_PARTIAL, and the last one gives RD_CHUNK_DONE with the
     bytes of the text.  A state can join one text after another.
   - Restart: the first messages of a text, and then all messages of a
     text with another serial, give the second text.
   - Damage: a message with an offset that is not the next offset,
     with another total than the messages before it, with bytes after
     the end of the text, or with another serial and an offset that is
     not 0, gives RD_CHUNK_REFUSED.  Each later message of the damaged
     text gives RD_CHUNK_REFUSED too, also a correct copy of the damaged
     message if it is not the first one.  A message of a text after its
     last message gives RD_CHUNK_REFUSED.  After that, a new text with
     another serial comes through.
   - Limit: a text with more bytes than the limit of the receiver
     gives RD_CHUNK_REFUSED at its first message, and it drops a text
     that did not come complete before it.  A text with the limit
     comes through.  The limit is the size of a text, a small size, a
     random size up to 2 * RD_MAX_CLIPBOARD_CHUNK + 1, or a size
     above RD_MAX_CLIPBOARD_SIZE.  All messages of a text with exactly
     LIMIT bytes give RD_CHUNK_DONE at the end, with the text, and the
     first message of a text with LIMIT + 1 bytes gives
     RD_CHUNK_REFUSED.
   - Part: rd_chunk_part_of gives the fields of a clipboard-data
     message, and no part for a message of a type that is not chunked.
   - Not chunked: rd_chunk_message with a type that is not chunked
     stops the program with an assertion failure.
   - Add message: rd_chunks_add_message with a message of a type that
     is not chunked gives RD_CHUNK_REFUSED, and does not change the
     state.

   - Image: the image-data messages of an image, in order, join to
     its bytes with the image limit RD_MAX_IMAGE_SIZE.  A message at
     the offset 0 with another image ID starts a new image, and drops
     the image before it.  An image with more than RD_MAX_IMAGE_SIZE
     bytes is refused at its first message.

   - Queue: the messages of a queue join to its text.  A queue has a
     message until the last message of its text, and is empty after
     it.  A new text in the middle of a text gives a new serial, and the
     messages after it join to the new text.  A cleared queue gives no
     message, and keeps its serial, so the next text has a new one.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "rdchunk.h"

enum { TRIALS = 300 };

/* The largest random text, in bytes.  */
enum { MAX_TEXT = 5 * RD_MAX_CLIPBOARD_CHUNK + 100 };

/* A random text.  */
struct text
{
  unsigned char *bytes;
  size_t size;
};

static const char *run_trial (uint64_t);
static bool split_law (uint64_t *);
static bool round_trip_law (uint64_t *);
static bool restart_law (uint64_t *);
static bool damage_law (uint64_t *);
static bool limit_law (uint64_t *);
static bool part_law (uint64_t *);
static bool not_chunked_law (uint64_t *);
static bool add_message_law (uint64_t *);
static bool chunked_types_example (uint64_t *);
static bool queue_law (uint64_t *);
static bool image_law (uint64_t *);
static struct text random_text (uint64_t *);
static enum rd_chunk_result join (struct rd_chunks *, uint32_t,
				  const struct text *);
static enum rd_chunk_result join_within (struct rd_chunks *, uint32_t,
					 const struct text *, size_t);
static bool chunked_p (enum rd_type);
static enum rd_type random_unchunked_type (uint64_t *);
static bool has_text (const struct rd_chunks *, const struct text *);
static struct rd_chunk_part chunk (uint32_t, const struct text *, uint32_t,
				   struct rd_message *);
static enum rd_chunk_result add (struct rd_chunks *,
				 const struct rd_chunk_part *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "split", split_law },
    { "round trip", round_trip_law },
    { "restart", restart_law },
    { "damage", damage_law },
    { "limit", limit_law },
    { "part", part_law },
    { "not chunked", not_chunked_law },
    { "add message", add_message_law },
    { "chunked types", chunked_types_example },
    { "queue", queue_law },
    { "image", image_law },
  };

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 1 : 0;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    {
      const char *failed = run_trial (base + trial);
      if (failed)
	{
	  fprintf (stderr,
		   "test-rdchunk: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-rdchunk: %d trials passed\n", TRIALS);
  return 0;
}

/* Run each law with SEED.  Return the name of the first law that
   fails, or NULL.  */
static const char *
run_trial (uint64_t seed)
{
  for (size_t i = 0; i < sizeof laws / sizeof *laws; i++)
    {
      uint64_t state = seed;
      if (!laws[i].check (&state))
	return laws[i].name;
    }
  return NULL;
}

static bool
split_law (uint64_t *state)
{
  struct text text = random_text (state);
  uint32_t serial = random_next (state);
  uint32_t count = rd_chunk_count (text.size);
  uint32_t expected = (text.size == 0 ? 1
		       : (text.size + RD_MAX_CLIPBOARD_CHUNK - 1)
			 / RD_MAX_CLIPBOARD_CHUNK);
  bool passed = count == expected;
  size_t offset = 0;
  for (uint32_t i = 0; passed && i < count; i++)
    {
      struct rd_message message;
      struct rd_chunk_part data = chunk (serial, &text, i, &message);
      bool last = i + 1 == count;
      passed = (message.type == RD_CLIPBOARD_DATA
		&& data.serial == serial && data.total == text.size
		&& data.offset == offset
		&& (last ? (text.size == 0 || data.nbytes > 0)
		    : data.nbytes == RD_MAX_CLIPBOARD_CHUNK)
		&& !memcmp (data.bytes, text.bytes + offset, data.nbytes));
      offset += data.nbytes;
    }
  free (text.bytes);
  return passed && offset == text.size;
}

static bool
round_trip_law (uint64_t *state)
{
  struct rd_chunks chunks = { 0 };
  bool passed = true;
  for (int i = 0; passed && i < 3; i++)
    {
      struct text text = random_text (state);
      passed = (join (&chunks, random_next (state), &text) == RD_CHUNK_DONE
		&& has_text (&chunks, &text));
      free (text.bytes);
    }
  rd_chunks_free (&chunks);
  return passed;
}

static bool
restart_law (uint64_t *state)
{
  struct rd_chunks chunks = { 0 };
  struct text first = random_text (state), second = random_text (state);
  uint32_t serial = random_next (state);
  uint32_t count = rd_chunk_count (first.size);
  uint32_t sent = random_below (state, count);
  bool passed = true;
  for (uint32_t i = 0; passed && i < sent; i++)
    {
      struct rd_message message;
      struct rd_chunk_part data = chunk (serial, &first, i, &message);
      passed = add (&chunks, &data) == RD_CHUNK_PARTIAL;
    }
  passed = (passed && join (&chunks, serial + 1, &second) == RD_CHUNK_DONE
	    && has_text (&chunks, &second));
  free (first.bytes);
  free (second.bytes);
  rd_chunks_free (&chunks);
  return passed;
}

/* The kinds of damage of the damage law.  */
enum damage { BAD_OFFSET, BAD_TOTAL, PAST_END, STRAY_SERIAL, DAMAGE_KINDS };

static bool
damage_law (uint64_t *state)
{
  struct rd_chunks chunks = { 0 };
  struct text text;
  do
    text = random_text (state);
  while (text.size == 0 && (free (text.bytes), true));
  uint32_t serial = random_next (state);
  uint32_t count = rd_chunk_count (text.size);
  uint32_t at = random_below (state, count);
  enum damage damage = random_below (state, DAMAGE_KINDS);
  /* The first message of a text sets its total, and starts a text
     with any serial.  */
  if ((damage == BAD_TOTAL || damage == STRAY_SERIAL) && at == 0)
    damage = BAD_OFFSET;
  bool passed = true;
  for (uint32_t i = 0; passed && i < at; i++)
    {
      struct rd_message message;
      struct rd_chunk_part data = chunk (serial, &text, i, &message);
      passed = add (&chunks, &data) == RD_CHUNK_PARTIAL;
    }
  struct rd_message message;
  struct rd_chunk_part data = chunk (serial, &text, at, &message);
  if (damage == BAD_OFFSET)
    data.offset += 1 + random_below (state, 3);
  else if (damage == BAD_TOTAL)
    data.total += 1 + random_below (state, 3);
  else if (damage == STRAY_SERIAL)
    data.serial += 1 + random_below (state, 3);
  else if (data.nbytes < RD_MAX_CLIPBOARD_CHUNK)
    /* The chunk ends one byte after the text.  */
    data.nbytes = data.total - data.offset + 1;
  else
    data.total = data.offset + data.nbytes - 1;
  passed = passed && add (&chunks, &data) == RD_CHUNK_REFUSED;
  /* A correct copy of the damaged message, and the messages after it.
     A correct first message starts the text again, so it is not
     part of this check.  */
  for (uint32_t i = at; passed && at > 0 && i < count; i++)
    {
      struct rd_chunk_part rest = chunk (serial, &text, i, &message);
      passed = add (&chunks, &rest) == RD_CHUNK_REFUSED;
    }
  struct text other = random_text (state);
  passed = (passed && join (&chunks, serial + 1, &other) == RD_CHUNK_DONE
	    && has_text (&chunks, &other));
  /* A message of OTHER after its last message.  */
  uint32_t last = rd_chunk_count (other.size) - 1;
  if (last > 0)
    {
      struct rd_chunk_part again = chunk (serial + 1, &other, last, &message);
      passed = passed && add (&chunks, &again) == RD_CHUNK_REFUSED;
    }
  free (text.bytes);
  free (other.bytes);
  rd_chunks_free (&chunks);
  return passed;
}

static bool
limit_law (uint64_t *state)
{
  static const unsigned char zeros[RD_MAX_CLIPBOARD_CHUNK];
  struct rd_chunks chunks = { 0 };
  struct text text = random_text (state);
  uint32_t serial = random_next (state);
  /* The limit of the receiver.  */
  size_t limits[] = { text.size, random_below (state, 3),
		      random_below (state, 2 * RD_MAX_CLIPBOARD_CHUNK + 2),
		      RD_MAX_CLIPBOARD_SIZE,
		      RD_MAX_CLIPBOARD_SIZE + 1 + random_below (state, 1000) };
  size_t limit = limits[random_below (state, sizeof limits / sizeof *limits)];
  bool partial = rd_chunk_count (text.size) > 1 && text.size <= limit;
  struct rd_message message;
  if (partial)
    {
      struct rd_chunk_part first = chunk (serial, &text, 0, &message);
      rd_chunks_add (&chunks, &first, limit);
    }
  uint32_t total = limit + 1 + random_below (state, 1000);
  struct rd_chunk_part data
    = { .serial = random_next (state), .offset = 0, .total = total,
	.nbytes = 1 + random_below (state, RD_MAX_CLIPBOARD_CHUNK),
	.bytes = zeros };
  if (data.nbytes > data.total)
    data.nbytes = data.total;
  bool passed = rd_chunks_add (&chunks, &data, limit) == RD_CHUNK_REFUSED;
  if (partial)
    {
      struct rd_chunk_part second = chunk (serial, &text, 1, &message);
      passed = (passed
		&& rd_chunks_add (&chunks, &second, limit) == RD_CHUNK_REFUSED);
    }
  free (text.bytes);
  data.total = limit;
  if (data.nbytes > data.total)
    data.nbytes = data.total;
  enum rd_chunk_result expected = (data.nbytes < data.total
				   ? RD_CHUNK_PARTIAL : RD_CHUNK_DONE);
  passed = passed && rd_chunks_add (&chunks, &data, limit) == expected;
  /* A whole text with exactly LIMIT bytes, here the size of TEXT.  */
  text = random_text (state);
  passed = (passed
	    && join_within (&chunks, serial, &text, text.size) == RD_CHUNK_DONE
	    && has_text (&chunks, &text));
  if (text.size > 0)
    {
      struct rd_chunk_part first = chunk (serial + 1, &text, 0, &message);
      passed = (passed && rd_chunks_add (&chunks, &first, text.size - 1)
			  == RD_CHUNK_REFUSED);
    }
  free (text.bytes);
  rd_chunks_free (&chunks);
  return passed;
}

static bool
part_law (uint64_t *state)
{
  struct text text = random_text (state);
  uint32_t serial = random_next (state);
  uint32_t index = random_below (state, rd_chunk_count (text.size));
  struct rd_message message;
  rd_chunk_message (RD_CLIPBOARD_DATA, serial, text.bytes, text.size, index,
		    &message);
  const struct rd_clipboard_data *data = &message.u.clipboard_data;
  struct rd_chunk_part part;
  bool passed = (rd_chunk_part_of (&message, &part)
		 && part.serial == data->serial
		 && part.offset == data->offset
		 && part.total == data->total
		 && part.nbytes == data->nbytes
		 && part.bytes == data->bytes);
  /* A message of a type that is not chunked has no part.  */
  struct rd_message other = { .type = random_unchunked_type (state) };
  passed = passed && !rd_chunk_part_of (&other, &part);
  free (text.bytes);
  return passed;
}

static bool
not_chunked_law (uint64_t *state)
{
  enum rd_type type = random_unchunked_type (state);
  pid_t child = fork ();
  if (child == 0)
    {
      /* The child must not write a core file or the message of the
	 abort.  */
      setrlimit (RLIMIT_CORE, &(struct rlimit) { 0, 0 });
      int null = open ("/dev/null", O_WRONLY);
      dup2 (null, STDERR_FILENO);
      static const unsigned char text[] = "text";
      struct rd_message message;
      rd_chunk_message (type, 1, text, sizeof text, 0, &message);
      _exit (0);
    }
  int status;
  return (child > 0 && waitpid (child, &status, 0) == child
	  && WIFSIGNALED (status) && WTERMSIG (status) == SIGABRT);
}

/* An example that does not use RD_CHUNKED_MESSAGES: a clipboard-data
   message has a part, and a hello message has none.  */
static bool
chunked_types_example (uint64_t *state)
{
  (void) state;
  struct rd_chunk_part part;
  struct rd_message clipboard = { .type = RD_CLIPBOARD_DATA };
  struct rd_message hello = { .type = RD_HELLO };
  return (rd_chunk_part_of (&clipboard, &part)
	  && !rd_chunk_part_of (&hello, &part));
}

static bool
add_message_law (uint64_t *state)
{
  struct rd_chunks chunks = { 0 };
  struct text text = random_text (state);
  uint32_t serial = random_next (state);
  /* Make the state of a receiver in the middle of TEXT.  */
  uint32_t sent = random_below (state, rd_chunk_count (text.size));
  struct rd_message message;
  for (uint32_t i = 0; i < sent; i++)
    {
      chunk (serial, &text, i, &message);
      rd_chunks_add_message (&chunks, &message, RD_MAX_CLIPBOARD_SIZE);
    }
  struct rd_message other = { .type = random_unchunked_type (state) };
  bool passed = (rd_chunks_add_message (&chunks, &other,
					RD_MAX_CLIPBOARD_SIZE)
		 == RD_CHUNK_REFUSED);
  /* The refusal does not change the state: the rest of TEXT completes
     it.  */
  enum rd_chunk_result result = RD_CHUNK_PARTIAL;
  for (uint32_t i = sent; i < rd_chunk_count (text.size); i++)
    {
      chunk (serial, &text, i, &message);
      result = rd_chunks_add_message (&chunks, &message,
				      RD_MAX_CLIPBOARD_SIZE);
    }
  passed = (passed && result == RD_CHUNK_DONE
	    && has_text (&chunks, &text));
  free (text.bytes);
  rd_chunks_free (&chunks);
  return passed;
}

static bool
queue_law (uint64_t *state)
{
  struct rd_chunk_queue queue = { 0 };
  struct rd_chunks chunks = { 0 };
  struct text first = random_text (state), second = random_text (state);
  bool passed = (!rd_chunk_queue_pending_p (&queue)
		 && rd_chunk_queue_set (&queue, first.bytes, first.size));
  uint32_t first_serial = queue.serial;
  /* Send some messages of FIRST, and then replace it with SECOND, or
     send all messages of FIRST.  */
  bool replace = random_below (state, 2);
  uint32_t sent = (replace ? random_below (state, rd_chunk_count (first.size))
		   : rd_chunk_count (first.size));
  struct rd_message message;
  enum rd_chunk_result result = RD_CHUNK_REFUSED;
  for (uint32_t i = 0; passed && i < sent; i++)
    {
      passed = rd_chunk_queue_next (&queue, &message);
      result = rd_chunks_add_message (&chunks, &message,
				      RD_MAX_CLIPBOARD_SIZE);
    }
  if (replace)
    passed = passed && rd_chunk_queue_pending_p (&queue);
  else
    passed = (passed && result == RD_CHUNK_DONE && has_text (&chunks, &first)
	      && !rd_chunk_queue_pending_p (&queue)
	      && !rd_chunk_queue_next (&queue, &message));
  passed = (passed && rd_chunk_queue_set (&queue, second.bytes, second.size)
	    && queue.serial != first_serial);
  while (passed && rd_chunk_queue_next (&queue, &message))
    result = rd_chunks_add_message (&chunks, &message,
				    RD_MAX_CLIPBOARD_SIZE);
  uint32_t second_serial = queue.serial;
  passed = (passed && result == RD_CHUNK_DONE && has_text (&chunks, &second)
	    && rd_chunk_queue_set (&queue, first.bytes, first.size));
  rd_chunk_queue_clear (&queue);
  passed = (passed && !rd_chunk_queue_pending_p (&queue)
	    && !rd_chunk_queue_next (&queue, &message)
	    && rd_chunk_queue_set (&queue, first.bytes, first.size)
	    && queue.serial != second_serial
	    && queue.serial != second_serial + 1);
  rd_chunk_queue_clear (&queue);
  free (first.bytes);
  free (second.bytes);
  rd_chunks_free (&chunks);
  return passed;
}

static bool
image_law (uint64_t *state)
{
  struct rd_chunks chunks = { 0 };
  struct text first = random_text (state), second = random_text (state);
  uint32_t image = random_next (state);
  struct rd_message message;
  /* Some messages of FIRST, then all messages of SECOND with another
     image ID.  */
  uint32_t sent = random_below (state, rd_chunk_count (first.size));
  bool passed = true;
  for (uint32_t i = 0; passed && i < sent; i++)
    {
      rd_chunk_message (RD_IMAGE_DATA, image, first.bytes, first.size, i,
			&message);
      passed = (rd_chunks_add_message (&chunks, &message, RD_MAX_IMAGE_SIZE)
		== RD_CHUNK_PARTIAL);
    }
  enum rd_chunk_result result = RD_CHUNK_REFUSED;
  for (uint32_t i = 0; passed && i < rd_chunk_count (second.size); i++)
    {
      rd_chunk_message (RD_IMAGE_DATA, image + 1, second.bytes, second.size,
			i, &message);
      result = rd_chunks_add_message (&chunks, &message, RD_MAX_IMAGE_SIZE);
    }
  passed = passed && result == RD_CHUNK_DONE && has_text (&chunks, &second);
  /* The first message of an image that is too large.  */
  static const unsigned char zeros[RD_MAX_IMAGE_CHUNK];
  uint32_t total = RD_MAX_IMAGE_SIZE + 1 + random_below (state, 1000);
  rd_chunk_message (RD_IMAGE_DATA, image + 2, zeros, total, 0, &message);
  passed = (passed
	    && (rd_chunks_add_message (&chunks, &message, RD_MAX_IMAGE_SIZE)
		== RD_CHUNK_REFUSED));
  free (first.bytes);
  free (second.bytes);
  rd_chunks_free (&chunks);
  return passed;
}

/* Return a new random text of STATE.  Free its bytes with free.  */
static struct text
random_text (uint64_t *state)
{
  struct text text;
  switch (random_below (state, 4))
    {
    case 0:
      text.size = random_below (state, 3);
      break;
    case 1:
      text.size = ((1 + random_below (state, 4)) * RD_MAX_CLIPBOARD_CHUNK
		   - 1 + random_below (state, 3));
      break;
    default:
      text.size = random_below (state, MAX_TEXT);
    }
  text.bytes = malloc (text.size + 1);
  for (size_t i = 0; i < text.size; i++)
    text.bytes[i] = random_next (state);
  return text;
}

/* Give all messages of TEXT with SERIAL to CHUNKS in order, with the
   clipboard limit.  Return the result of the last one, or
   RD_CHUNK_REFUSED if a message before it does not give
   RD_CHUNK_PARTIAL.  */
static enum rd_chunk_result
join (struct rd_chunks *chunks, uint32_t serial, const struct text *text)
{
  return join_within (chunks, serial, text, RD_MAX_CLIPBOARD_SIZE);
}

/* Do as join does, with the limit LIMIT.  */
static enum rd_chunk_result
join_within (struct rd_chunks *chunks, uint32_t serial,
	     const struct text *text, size_t limit)
{
  uint32_t count = rd_chunk_count (text->size);
  for (uint32_t i = 0; i < count; i++)
    {
      struct rd_message message;
      struct rd_chunk_part data = chunk (serial, text, i, &message);
      enum rd_chunk_result result = rd_chunks_add (chunks, &data, limit);
      if (i + 1 == count)
	return result;
      if (result != RD_CHUNK_PARTIAL)
	return RD_CHUNK_REFUSED;
    }
  return RD_CHUNK_REFUSED;
}

/* Return true if TYPE is a chunked type.  */
static bool
chunked_p (enum rd_type type)
{
#define CHUNKED_TYPE_P(ENUM, NAME, SERIAL) || type == RD_##ENUM
  return false RD_CHUNKED_MESSAGES (CHUNKED_TYPE_P);
#undef CHUNKED_TYPE_P
}

/* Return a random message type of STATE that is not chunked.  */
static enum rd_type
random_unchunked_type (uint64_t *state)
{
  enum rd_type type;
  /* The value 0 is not a message type.  */
  do
    type = 1 + random_below (state, RD_TYPE_LIMIT - 1);
  while (chunked_p (type));
  return type;
}

/* Return true if CHUNKS has the complete TEXT.  */
static bool
has_text (const struct rd_chunks *chunks, const struct text *text)
{
  return (chunks->total == text->size
	  && (text->size == 0
	      || !memcmp (chunks->bytes, text->bytes, text->size)));
}

/* Set MESSAGE to the clipboard-data message INDEX of TEXT with
   SERIAL, and return its part.  The bytes of the part are in
   MESSAGE.  */
static struct rd_chunk_part
chunk (uint32_t serial, const struct text *text, uint32_t index,
       struct rd_message *message)
{
  rd_chunk_message (RD_CLIPBOARD_DATA, serial, text->bytes, text->size,
		    index, message);
  struct rd_chunk_part part;
  rd_chunk_part_of (message, &part);
  return part;
}

/* Add PART to CHUNKS with the clipboard limit.  */
static enum rd_chunk_result
add (struct rd_chunks *chunks, const struct rd_chunk_part *part)
{
  return rd_chunks_add (chunks, part, RD_MAX_CLIPBOARD_SIZE);
}

/* Return a random number from 0 to LIMIT - 1.  */
static uint64_t
random_below (uint64_t *state, uint64_t limit)
{
  return random_next (state) % limit;
}

/* Return the next number of the generator splitmix64.  */
static uint64_t
random_next (uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}
