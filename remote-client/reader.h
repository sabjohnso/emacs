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

/* A reader decodes a stream of bytes from the server into messages.
   The bytes can come in chunks of any size.  The reader does no I/O:
   the caller reads the bytes and gives them to the reader.  */

#ifndef REMOTE_CLIENT_READER_H
#define REMOTE_CLIENT_READER_H

#include <stdbool.h>
#include <stddef.h>

#include "rdproto.h"

/* The function that gets each message.  DATA is the value that the
   caller gave to rc_reader_new.  */
typedef void rc_message_function (void *data,
				  const struct rd_message *message);

struct rc_reader;

/* Return a new reader that calls RECEIVE with DATA for each message.  */
extern struct rc_reader *rc_reader_new (rc_message_function *receive,
					void *data);
extern void rc_reader_free (struct rc_reader *reader);

/* Give the SIZE bytes BYTES of the stream to READER.  Call the
   function of READER for each message that is complete now, in the
   order of the stream.  Skip a frame of a type that the reader does
   not know.  Return false if the stream has an error.  After an
   error, the reader ignores all bytes.  */
extern bool rc_reader_feed (struct rc_reader *reader,
			    const unsigned char *bytes, size_t size);

/* Return the description of the error of the stream of READER, or
   NULL if the stream has no error.  */
extern const char *rc_reader_error (const struct rc_reader *reader);

#endif /* REMOTE_CLIENT_READER_H */
