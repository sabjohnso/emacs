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

/* A desktop with a scale K has K times K device pixels for each
   logical pixel.  GTK gives the positions and the sizes of a window
   in logical pixels.  The server gives the positions and the sizes of
   a frame in device pixels (rdproto.h, the scale block), so the
   client paints each pixel of a frame on one device pixel.  These
   functions change one kind of pixel to the other.  They need no
   display.  */

#ifndef REMOTE_CLIENT_SCALE_H
#define REMOTE_CLIENT_SCALE_H

#include <stdint.h>

#include "rdproto.h"
#include "sizes.h"

/* Return the scale that the client uses for the scale SCALE of GTK:
   SCALE, but at least 1 and at most RD_MAX_SCALE.  */
extern int rc_scale_clamp (int scale);

/* Return the device point of the logical point LOGICAL, at the scale
   SCALE.  A point of a pointer event has a fraction.  */
extern double rc_scale_point (double logical, int scale);

/* Return the size in device pixels of the logical size SIZE, at the
   scale SCALE.  A width or a height above INT_MAX becomes INT_MAX,
   because a resize message takes an int.  */
extern struct rc_size rc_scale_to_device (struct rc_size size, int scale);

/* Return the smallest logical size that has the device size SIZE, at
   the scale SCALE.  */
extern struct rc_size rc_scale_to_logical (struct rc_size size, int scale);

/* Return the logical position of the pixel that has the device
   position POSITION, at the scale SCALE.  */
extern int64_t rc_scale_position (int64_t position, int scale);

#endif /* REMOTE_CLIENT_SCALE_H */
