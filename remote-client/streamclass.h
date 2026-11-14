/* The interface between a stream and its kind.


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

/* Each kind of stream gives the functions of stream.h for its kind.
   Only the files of the kinds include this file.  The struct of a
   kind starts with a struct rc_stream.  */

#ifndef REMOTE_CLIENT_STREAMCLASS_H
#define REMOTE_CLIENT_STREAMCLASS_H

#include <stdbool.h>
#include <stddef.h>

struct rc_stream;

/* The functions of a kind of stream.  They have the contracts of the
   functions of stream.h with the same names.  CLOSE closes the socket
   and frees the stream.  */
struct rc_stream_class
{
  ptrdiff_t (*read) (struct rc_stream *, void *, size_t);
  ptrdiff_t (*write) (struct rc_stream *, const void *, size_t);
  short (*events) (const struct rc_stream *, short);
  bool (*buffered_p) (const struct rc_stream *);
  void (*close) (struct rc_stream *);
};

/* The common part of each stream: its kind and its socket.  */
struct rc_stream
{
  const struct rc_stream_class *class;
  int fd;
};

#endif /* REMOTE_CLIENT_STREAMCLASS_H */
