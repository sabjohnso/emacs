/* The paint of a root frame and its children for the remote display
   client.

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

#include "compose.h"

/* The data of one call of rc_compose.  */
struct composition
{
  cairo_t *cr;
  rc_compose_surface_function surface;
  void *data;
};

static void paint_layer (const struct rc_layer *, void *);

void
rc_compose (cairo_t *cr, const struct rc_stack *stack, uint32_t root,
	    rc_compose_surface_function surface, void *data)
{
  struct composition composition = { cr, surface, data };
  rc_stack_each_layer (stack, root, paint_layer, &composition);
}

/* Paint the surface of LAYER at its origin, clipped to its clip.  The
   clip of the layer is already clipped to each ancestor.  DATA is the
   composition.  */
static void
paint_layer (const struct rc_layer *layer, void *data)
{
  struct composition *composition = data;
  cairo_surface_t *surface
    = composition->surface (composition->data, layer->frame);
  if (!surface)
    return;
  cairo_t *cr = composition->cr;
  cairo_save (cr);
  cairo_rectangle (cr, layer->clip.x, layer->clip.y, layer->clip.width,
		   layer->clip.height);
  cairo_clip (cr);
  cairo_set_source_surface (cr, surface, layer->x, layer->y);
  cairo_paint (cr);
  cairo_restore (cr);
}
