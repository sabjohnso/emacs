/* The scale of the remote display client.

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

/* A logical size covers a device size when it has at least as many
   device pixels, so the logical size of a device size rounds up.  A
   logical position is the pixel that has the device position, so it
   rounds down, also for a negative position.  */

#include <config.h>

#include <limits.h>

#include "scale.h"

static uint32_t device_length (uint32_t, int);
static uint32_t logical_length (uint32_t, int);

int
rc_scale_clamp (int scale)
{
  return scale < 1 ? 1 : scale > RD_MAX_SCALE ? RD_MAX_SCALE : scale;
}

double
rc_scale_point (double logical, int scale)
{
  return logical * scale;
}

struct rc_size
rc_scale_to_device (struct rc_size size, int scale)
{
  return (struct rc_size) { device_length (size.width, scale),
			    device_length (size.height, scale) };
}

struct rc_size
rc_scale_to_logical (struct rc_size size, int scale)
{
  return (struct rc_size) { logical_length (size.width, scale),
			    logical_length (size.height, scale) };
}

int64_t
rc_scale_position (int64_t position, int scale)
{
  int64_t quotient = position / scale;
  return quotient * scale > position ? quotient - 1 : quotient;
}

/* Return the number of device pixels of LENGTH logical pixels at
   SCALE, but at most INT_MAX, because a resize message takes an
   int.  */
static uint32_t
device_length (uint32_t length, int scale)
{
  uint64_t device = (uint64_t) length * scale;
  return device > INT_MAX ? INT_MAX : device;
}

/* Return the smallest number of logical pixels at SCALE that has
   LENGTH device pixels.  */
static uint32_t
logical_length (uint32_t length, int scale)
{
  return ((uint64_t) length + scale - 1) / scale;
}
