/* The buffer of one direction of the relay of remote-client.

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

/* The relay copies the bytes of one connection to another connection.
   A struct rc_flow holds the bytes that the relay read and did not yet
   write.  The flow takes new bytes only when it is empty, so a slow
   reader stops a fast writer.  A write can take only a part of the
   bytes, so the flow keeps the rest.

   The flow does no input and no output: the caller reads into the
   space of the flow, and writes the bytes of the flow.  */

#ifndef REMOTE_CLIENT_FLOW_H
#define REMOTE_CLIENT_FLOW_H

#include <stdbool.h>
#include <stddef.h>

/* The most bytes of a flow.  */
enum { RC_FLOW_SIZE = 65536 };

/* The bytes of a flow.  Only the functions of flow.c use the fields.
   The type is not opaque, so the caller can keep a flow in a struct
   with no allocation.  */
struct rc_flow
{
  size_t start, end;
  unsigned char bytes[RC_FLOW_SIZE];
};

/* Make FLOW empty.  */
extern void rc_flow_init (struct rc_flow *flow);

/* Return true if FLOW holds no bytes.  */
extern bool rc_flow_empty_p (const struct rc_flow *flow);

/* Return the space for new bytes of FLOW, and put its size in *SIZE.
   The size is RC_FLOW_SIZE if FLOW is empty, and 0 if it is not.  */
extern unsigned char *rc_flow_space (struct rc_flow *flow, size_t *size);

/* Add the first COUNT bytes of the space of FLOW to its bytes.  FLOW
   must be empty, and COUNT must not be more than RC_FLOW_SIZE.  */
extern void rc_flow_fill (struct rc_flow *flow, size_t count);

/* Return the bytes of FLOW, and put their number in *COUNT.  */
extern const unsigned char *rc_flow_bytes (const struct rc_flow *flow,
					   size_t *count);

/* Remove the first COUNT bytes of FLOW.  COUNT must not be more than
   the number of its bytes.  */
extern void rc_flow_drain (struct rc_flow *flow, size_t count);

#endif /* REMOTE_CLIENT_FLOW_H */
