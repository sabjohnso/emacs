/* Tests of the reader module of the remote display client.

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

/* The laws of the reader:

   - The reader gives the messages of a stream in their order, for
     each division of the stream into chunks.
   - The reader skips a frame of a type that it does not know, and
     gives the messages after it.
   - After a frame with an error, the reader gives no more messages,
     and reports the error.  Each later feed returns false.
   - prefix: for a stream that stops at any byte, the reader gives the
     messages that are complete in it, and reports no error.

   A trial encodes random messages to a stream, and puts frames of
   unknown types between them.  Some trials put a frame with an error
   at a random position, and some stop the stream at a random byte.
   The trial feeds the stream to a reader in random chunks.

   With no argument, the program runs TRIALS trials from a seed that
   it gets from the clock and the process ID.  With the argument SEED,
   it runs only the trial of that seed.  */

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rdproto.h"
#include "reader.h"

enum { TRIALS = 300, MAX_MESSAGES = 30 };

/* The messages that a reader gave.  */
struct received
{
  struct rd_message messages[MAX_MESSAGES];
  int count;
};

/* A stream of bytes.  */
struct stream
{
  unsigned char *bytes;
  size_t size;
};

static bool run_examples (void);
static bool menu_item_example (void);
static bool menu_values_example (void);
static bool run_trial (uint64_t);
static bool same_messages (const struct rd_message *, int,
			   const struct received *);
static void receive (void *, const struct rd_message *);
static void feed_in_chunks (struct rc_reader *, struct stream,
			    uint64_t *);
static void add_message (struct stream *, const struct rd_message *);
static void add_unknown_frame (struct stream *, uint64_t *);
static void add_bad_frame (struct stream *, uint64_t *);
static void add_bytes (struct stream *, const unsigned char *, size_t);
static struct rd_message random_message (uint64_t *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 0 : 1;
  if (!run_examples () || !menu_item_example ()
      || !menu_values_example ())
    return 1;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    if (!run_trial (base + trial))
      {
	fprintf (stderr, "test-reader: seed %llu failed\n",
		 (unsigned long long) (base + trial));
	return 1;
      }
  printf ("test-reader: the examples and %d trials passed\n", TRIALS);
  return 0;
}

/* Return true if the examples of errors pass.  */
static bool
run_examples (void)
{
  /* A frame with a length of 0 has no type byte.  */
  static const unsigned char no_type[] = { 0, 0, 0, 0 };
  /* A frame longer than RD_MAX_LENGTH.  */
  static const unsigned char too_long[] = { 0x7f, 0, 0, 0, 1 };
  struct rd_message hello = { .type = RD_HELLO, .u.hello = { 7 } };
  bool passed = true;
  const unsigned char *bad[] = { no_type, too_long };
  size_t bad_size[] = { sizeof no_type, sizeof too_long };
  for (int i = 0; i < 2; i++)
    {
      struct stream stream = { 0 };
      add_message (&stream, &hello);
      add_bytes (&stream, bad[i], bad_size[i]);
      add_message (&stream, &hello);
      struct received received = { .count = 0 };
      struct rc_reader *reader = rc_reader_new (receive, &received);
      bool ok = rc_reader_feed (reader, stream.bytes, stream.size);
      if (ok || !rc_reader_error (reader) || received.count != 1)
	{
	  fprintf (stderr, "test-reader: example %d: the reader did not "
		   "stop at the error\n", i);
	  passed = false;
	}
      rc_reader_free (reader);
      free (stream.bytes);
    }
  return passed;
}

/* Return true if the kinds and the flags of a menu entry have their
   values of the wire format.  */
static bool
menu_values_example (void)
{
  bool passed = (RD_MENU_KIND_ITEM == 0 && RD_MENU_KIND_SEPARATOR == 1
		 && RD_MENU_KIND_SUBMENU_START == 2
		 && RD_MENU_KIND_SUBMENU_END == 3
		 && RD_MENU_FLAG_ENABLED == 1 && RD_MENU_FLAG_TOGGLE == 2
		 && RD_MENU_FLAG_RADIO == 4 && RD_MENU_FLAG_SELECTED == 8);
  if (!passed)
    fprintf (stderr, "test-reader: a menu kind or flag has a wrong value\n");
  return passed;
}

/* Return true if a menu-item message has the bytes of the wire format,
   and the reader gives the same message.  The type byte of the menu
   block changes when an earlier block gets a new type, so the example
   uses RD_MENU_ITEM and not a literal byte.  */
static bool
menu_item_example (void)
{
  struct rd_message item = {
    .type = RD_MENU_ITEM,
    .u.menu_item = {
      .serial = 7,
      .kind = RD_MENU_KIND_SUBMENU_START,
      .flags = RD_MENU_FLAG_ENABLED | RD_MENU_FLAG_SELECTED,
      .id = 0x0102,
      .nlabel = 2, .label = { 'a', 'b' },
      .nkey = 1, .key = { 'k' },
    },
  };
  /* Length 18, the type, serial 7, kind 2, flags 9, id 0x0102, 2 label
     bytes, 1 key byte.  */
  const unsigned char expected[] = {
    0, 0, 0, 18, RD_MENU_ITEM, 0, 0, 0, 7, 2, 9, 0, 0, 1, 2,
    0, 2, 'a', 'b', 0, 1, 'k',
  };
  struct stream stream = { 0 };
  add_message (&stream, &item);
  struct received received = { .count = 0 };
  struct rc_reader *reader = rc_reader_new (receive, &received);
  rc_reader_feed (reader, stream.bytes, stream.size);
  bool passed = (stream.size == sizeof expected
		 && memcmp (stream.bytes, expected, sizeof expected) == 0
		 && same_messages (&item, 1, &received));
  rc_reader_free (reader);
  free (stream.bytes);
  if (!passed)
    fprintf (stderr, "test-reader: the menu-item example failed\n");
  return passed;
}

/* Run the trial of SEED.  Return true if it passes.  */
static bool
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  struct rd_message sent[MAX_MESSAGES];
  size_t ends[MAX_MESSAGES];
  int nsent = random_below (&state, MAX_MESSAGES + 1);
  int bad_at = (random_below (&state, 4) == 0
		? (int) random_below (&state, nsent + 1) : -1);
  struct stream stream = { 0 };
  for (int i = 0; i <= nsent; i++)
    {
      while (random_below (&state, 4) == 0)
	add_unknown_frame (&stream, &state);
      if (i == bad_at)
	add_bad_frame (&stream, &state);
      if (i == nsent)
	break;
      sent[i] = random_message (&state);
      add_message (&stream, &sent[i]);
      ends[i] = stream.size;
    }
  /* A stream with an error does not stop early, so the error is in
     it.  */
  size_t cut = (bad_at < 0 && random_below (&state, 4) == 0
		? random_below (&state, stream.size + 1) : stream.size);
  int expected = bad_at < 0 ? nsent : bad_at;
  while (expected > 0 && ends[expected - 1] > cut)
    expected--;
  struct received received = { .count = 0 };
  struct rc_reader *reader = rc_reader_new (receive, &received);
  feed_in_chunks (reader, (struct stream) { stream.bytes, cut }, &state);
  /* After an error, a feed of one more byte returns false.  */
  unsigned char more = 0;
  bool error = rc_reader_error (reader) != NULL;
  bool passed = (error == (bad_at >= 0)
		 && (!error || !rc_reader_feed (reader, &more, 1))
		 && same_messages (sent, expected, &received));
  rc_reader_free (reader);
  free (stream.bytes);
  return passed;
}

/* Return true if RECEIVED has the N messages SENT.  Two messages are
   the same if their encoded bytes are the same.  */
static bool
same_messages (const struct rd_message *sent, int n,
	       const struct received *received)
{
  if (received->count != n)
    return false;
  for (int i = 0; i < n; i++)
    {
      struct stream a = { 0 }, b = { 0 };
      add_message (&a, &sent[i]);
      add_message (&b, &received->messages[i]);
      bool same = (a.size == b.size
		   && memcmp (a.bytes, b.bytes, a.size) == 0);
      free (a.bytes);
      free (b.bytes);
      if (!same)
	return false;
    }
  return true;
}

/* The function of a reader that records each MESSAGE in DATA, a
   struct received.  */
static void
receive (void *data, const struct rd_message *message)
{
  struct received *received = data;
  if (received->count < MAX_MESSAGES)
    received->messages[received->count] = *message;
  received->count++;
}

/* Feed STREAM to READER in chunks of random sizes.  Some chunks are
   empty.  */
static void
feed_in_chunks (struct rc_reader *reader, struct stream stream,
		uint64_t *state)
{
  size_t start = 0;
  while (start < stream.size)
    {
      size_t size = random_below (state, 4) == 0 ? stream.size - start
		    : random_below (state, 9);
      if (size > stream.size - start)
	size = stream.size - start;
      rc_reader_feed (reader, stream.bytes + start, size);
      start += size;
    }
}

/* Add the bytes of MESSAGE to STREAM.  */
static void
add_message (struct stream *stream, const struct rd_message *message)
{
  size_t size = rd_encoded_size (message);
  unsigned char *bytes = malloc (size);
  rd_encode (message, bytes);
  add_bytes (stream, bytes, size);
  free (bytes);
}

/* Add a frame of a type that the reader does not know to STREAM.  The
   type is 0, or above the last type.  Some frames are long.  */
static void
add_unknown_frame (struct stream *stream, uint64_t *state)
{
  uint32_t length = 1 + (random_below (state, 16) == 0
			 ? random_below (state, 100000)
			 : random_below (state, 60));
  unsigned char *frame = malloc (4 + length);
  frame[0] = length >> 24;
  frame[1] = length >> 16;
  frame[2] = length >> 8;
  frame[3] = length;
  uint32_t types = 256 - RD_TYPE_LIMIT + 1;
  uint32_t type = random_below (state, types);
  frame[4] = type == 0 ? 0 : RD_TYPE_LIMIT + type - 1;
  for (uint32_t i = 1; i < length; i++)
    frame[4 + i] = random_next (state);
  add_bytes (stream, frame, 4 + length);
  free (frame);
}

/* Add a frame with an error to STREAM: a frame with no type byte, a
   frame longer than RD_MAX_LENGTH, or a frame of a known type that is
   too short for the type.  */
static void
add_bad_frame (struct stream *stream, uint64_t *state)
{
  static const unsigned char no_type[] = { 0, 0, 0, 0 };
  static const unsigned char too_long[] = { 1, 0, 0, 1, RD_HELLO };
  static const unsigned char too_short[] = { 0, 0, 0, 1, RD_DEFINE_FRAME };
  switch (random_below (state, 3))
    {
    case 0:
      add_bytes (stream, no_type, sizeof no_type);
      break;
    case 1:
      add_bytes (stream, too_long, sizeof too_long);
      break;
    default:
      add_bytes (stream, too_short, sizeof too_short);
      break;
    }
}

/* Add the SIZE BYTES to STREAM.  */
static void
add_bytes (struct stream *stream, const unsigned char *bytes,
	   size_t size)
{
  stream->bytes = realloc (stream->bytes, stream->size + size + 1);
  memcpy (stream->bytes + stream->size, bytes, size);
  stream->size += size;
}

/* Return a random message of a random type, with random values in the
   limits of each field.  */
static struct rd_message
random_message (uint64_t *state)
{
  struct rd_message message = { .type = 1 + random_below (state,
							  RD_TYPE_LIMIT
							  - 1) };
  const struct rd_format *format = &rd_formats[message.type];
  for (int i = 0; i < format->nfields; i++)
    {
      const struct rd_field *f = &format->fields[i];
      int count = 1;
      if (f->max_count)
	{
	  count = random_below (state, f->max_count + 1);
	  rd_set_array_count (&message, f, count);
	}
      uint64_t range = f->value->max - f->value->min + 1;
      for (int j = 0; j < count; j++)
	rd_set_field_value (&message, f, j,
			    f->value->min + random_below (state, range));
    }
  return message;
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
