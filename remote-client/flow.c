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

#include <config.h>

#include <assert.h>

#include "flow.h"

void
rc_flow_init (struct rc_flow *flow)
{
  flow->start = flow->end = 0;
}

bool
rc_flow_empty_p (const struct rc_flow *flow)
{
  return flow->start == flow->end;
}

unsigned char *
rc_flow_space (struct rc_flow *flow, size_t *size)
{
  *size = rc_flow_empty_p (flow) ? RC_FLOW_SIZE : 0;
  return flow->bytes;
}

void
rc_flow_fill (struct rc_flow *flow, size_t count)
{
  assert (rc_flow_empty_p (flow) && count <= RC_FLOW_SIZE);
  flow->start = 0;
  flow->end = count;
}

const unsigned char *
rc_flow_bytes (const struct rc_flow *flow, size_t *count)
{
  *count = flow->end - flow->start;
  return flow->bytes + flow->start;
}

void
rc_flow_drain (struct rc_flow *flow, size_t count)
{
  assert (count <= flow->end - flow->start);
  flow->start += count;
  if (rc_flow_empty_p (flow))
    rc_flow_init (flow);
}
