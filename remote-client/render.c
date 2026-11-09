/* The render module of the remote display client.

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

/* Each draw clips its rectangle to the surface before it uses Cairo.
   A position and a size of the protocol have 32 bits, so their sum
   can need 33 bits.  The clip uses 64-bit integers, and gives Cairo
   only positions and sizes inside the surface.  Each draw fills
   rectangles at integer positions, with no antialias and the
   operator CAIRO_OPERATOR_SOURCE, so each pixel gets exactly the
   color of the message.

   The client cannot continue with no memory, so a failed allocation
   of the frame table stops the program.  */

#include <config.h>

#include <stdbool.h>
#include <stdlib.h>

#include "render.h"

/* A frame: its ID, and its surface.  */
struct rc_frame
{
  uint32_t id;
  cairo_surface_t *surface;
};

/* The frames that the server defined, in no order.  */
struct rc_renderer
{
  struct rc_frame *frames;
  int nframes;
  int size;
};

/* A rectangle with 64-bit fields.  */
struct rc_rect
{
  int64_t x, y, width, height;
};

static void define_frame (struct rc_renderer *,
			  const struct rd_define_frame *);
static void delete_frame (struct rc_renderer *, uint32_t);
static void clear_area (cairo_surface_t *, const struct rd_clear_area *);
static void copy_area (cairo_surface_t *, const struct rd_copy_area *);
static void draw_cursor (cairo_surface_t *,
			 const struct rd_draw_cursor *);
static void draw_hollow_box (cairo_t *, struct rc_rect);
static void draw_fringe (cairo_surface_t *,
			 const struct rd_draw_fringe *);
static void fill_fringe_bits (cairo_surface_t *,
			      const struct rd_draw_fringe *,
			      struct rc_rect, bool);
static bool fringe_bit (uint16_t, int, int);
static cairo_surface_t *snapshot (cairo_surface_t *, struct rc_rect);
static cairo_surface_t *resized_copy (cairo_surface_t *, uint32_t,
				      uint32_t);
static struct rc_frame *find_frame (struct rc_renderer *, uint32_t);
static struct rc_frame *add_frame (struct rc_renderer *, uint32_t);
static cairo_t *start_draw (cairo_surface_t *, uint32_t);
static cairo_t *start_copy (cairo_surface_t *, cairo_surface_t *,
			    int64_t, int64_t);
static void set_color (cairo_t *, uint32_t);
static void fill_rect (cairo_t *, struct rc_rect);
static struct rc_rect clip_rect (struct rc_rect, cairo_surface_t *);
static struct rc_rect intersection (struct rc_rect, struct rc_rect);

struct rc_renderer *
rc_renderer_new (void)
{
  struct rc_renderer *renderer = calloc (1, sizeof *renderer);
  if (!renderer)
    abort ();
  return renderer;
}

void
rc_renderer_free (struct rc_renderer *renderer)
{
  for (int i = 0; i < renderer->nframes; i++)
    cairo_surface_destroy (renderer->frames[i].surface);
  free (renderer->frames);
  free (renderer);
}

void
rc_apply (struct rc_renderer *renderer, const struct rd_message *message)
{
  const struct rd_clear_area *clear = &message->u.clear_area;
  const struct rd_copy_area *copy = &message->u.copy_area;
  const struct rd_draw_cursor *cursor = &message->u.draw_cursor;
  const struct rd_draw_fringe *fringe = &message->u.draw_fringe;
  switch (message->type)
    {
    case RD_DEFINE_FRAME:
      define_frame (renderer, &message->u.define_frame);
      break;
    case RD_DELETE_FRAME:
      delete_frame (renderer, message->u.delete_frame.frame);
      break;
    case RD_CLEAR_AREA:
      clear_area (rc_frame_surface (renderer, clear->frame), clear);
      break;
    case RD_COPY_AREA:
      copy_area (rc_frame_surface (renderer, copy->frame), copy);
      break;
    case RD_DRAW_CURSOR:
      draw_cursor (rc_frame_surface (renderer, cursor->frame), cursor);
      break;
    case RD_DRAW_FRINGE:
      draw_fringe (rc_frame_surface (renderer, fringe->frame), fringe);
      break;
    default:
      break;
    }
}

cairo_surface_t *
rc_frame_surface (struct rc_renderer *renderer, uint32_t frame)
{
  struct rc_frame *f = find_frame (renderer, frame);
  return f ? f->surface : NULL;
}

/* Make the frame of DEFINE, or give it the new size.  The pixels that
   are in both sizes keep their colors, and the new pixels are 0.  If
   the size is too large, or Cairo cannot make the surface, the frame
   does not exist after this call.  */
static void
define_frame (struct rc_renderer *renderer,
	      const struct rd_define_frame *define)
{
  struct rc_frame *frame = find_frame (renderer, define->frame);
  cairo_surface_t *old = frame ? frame->surface : NULL;
  cairo_surface_t *surface = resized_copy (old, define->width,
					   define->height);
  if (!surface)
    {
      delete_frame (renderer, define->frame);
      return;
    }
  if (!frame)
    frame = add_frame (renderer, define->frame);
  cairo_surface_destroy (old);
  frame->surface = surface;
}

/* Remove the frame ID, if it exists.  */
static void
delete_frame (struct rc_renderer *renderer, uint32_t id)
{
  struct rc_frame *frame = find_frame (renderer, id);
  if (!frame)
    return;
  cairo_surface_destroy (frame->surface);
  *frame = renderer->frames[--renderer->nframes];
}

/* Apply the clear-area message CLEAR to SURFACE, if it is not NULL.  */
static void
clear_area (cairo_surface_t *surface, const struct rd_clear_area *clear)
{
  if (!surface)
    return;
  cairo_t *cr = start_draw (surface, clear->color);
  fill_rect (cr, (struct rc_rect) { clear->x, clear->y, clear->width,
				    clear->height });
  cairo_destroy (cr);
}

/* Apply the copy-area message COPY to SURFACE, if it is not NULL.  The
   source can overlap the destination, so the source pixels go to a
   temporary surface first.  Cairo 1.18 gives incorrect pixels for an
   overlap when the source of a draw is its target.  */
static void
copy_area (cairo_surface_t *surface, const struct rd_copy_area *copy)
{
  if (!surface)
    return;
  int64_t dx = (int64_t) copy->to_x - copy->x;
  int64_t dy = (int64_t) copy->to_y - copy->y;
  struct rc_rect source = clip_rect ((struct rc_rect) { copy->x, copy->y,
							copy->width,
							copy->height },
				     surface);
  struct rc_rect to = clip_rect ((struct rc_rect) { source.x + dx,
						    source.y + dy,
						    source.width,
						    source.height },
				 surface);
  if (to.width == 0 || to.height == 0)
    return;
  cairo_surface_t *pixels
    = snapshot (surface, (struct rc_rect) { to.x - dx, to.y - dy,
					    to.width, to.height });
  cairo_t *cr = start_copy (surface, pixels, to.x, to.y);
  fill_rect (cr, to);
  cairo_destroy (cr);
  cairo_surface_destroy (pixels);
}

/* Apply the draw-cursor message CURSOR to SURFACE, if it is not
   NULL.  */
static void
draw_cursor (cairo_surface_t *surface, const struct rd_draw_cursor *cursor)
{
  if (!surface)
    return;
  struct rc_rect rect = { cursor->x, cursor->y, cursor->width,
			  cursor->height };
  cairo_t *cr = start_draw (surface, cursor->color);
  switch (cursor->kind)
    {
    case RD_CURSOR_BOX:
    case RD_CURSOR_BAR:
    case RD_CURSOR_HBAR:
      fill_rect (cr, rect);
      break;
    case RD_CURSOR_HOLLOW_BOX:
      draw_hollow_box (cr, rect);
      break;
    default:
      break;
    }
  cairo_destroy (cr);
}

/* Fill the pixels at the edges of RECT with the source of CR.  */
static void
draw_hollow_box (cairo_t *cr, struct rc_rect rect)
{
  if (rect.width == 0 || rect.height == 0)
    return;
  struct rc_rect edges[] =
    {
      { rect.x, rect.y, rect.width, 1 },
      { rect.x, rect.y + rect.height - 1, rect.width, 1 },
      { rect.x, rect.y, 1, rect.height },
      { rect.x + rect.width - 1, rect.y, 1, rect.height },
    };
  for (int i = 0; i < 4; i++)
    fill_rect (cr, edges[i]);
}

/* Apply the draw-fringe message FRINGE to SURFACE, if it is not
   NULL.  */
static void
draw_fringe (cairo_surface_t *surface, const struct rd_draw_fringe *fringe)
{
  if (!surface)
    return;
  struct rc_rect area = clip_rect ((struct rc_rect) { fringe->x, fringe->y,
						      fringe->width,
						      fringe->nrows },
				   surface);
  fill_fringe_bits (surface, fringe, area, true);
  if (!fringe->overlay)
    fill_fringe_bits (surface, fringe, area, false);
}

/* Fill each pixel of AREA of SURFACE whose bit in FRINGE is BIT with
   the color of that bit.  AREA must be inside SURFACE and inside the
   bitmap.  Each pixel is one rectangle in the path of the color.  */
static void
fill_fringe_bits (cairo_surface_t *surface,
		  const struct rd_draw_fringe *fringe, struct rc_rect area,
		  bool bit)
{
  cairo_t *cr = start_draw (surface, (bit ? fringe->foreground
				      : fringe->background));
  for (int64_t y = area.y; y < area.y + area.height; y++)
    for (int64_t x = area.x; x < area.x + area.width; x++)
      if (fringe_bit (fringe->rows[y - fringe->y], fringe->width,
		      x - fringe->x) == bit)
	cairo_rectangle (cr, x, y, 1, 1);
  cairo_fill (cr);
  cairo_destroy (cr);
}

/* Return the bit of the pixel COLUMN of a row BITS of a fringe of
   WIDTH pixels.  Bit 0 is the rightmost pixel.  A pixel to the left of
   bit 15 has the bit 0.  */
static bool
fringe_bit (uint16_t bits, int width, int column)
{
  int bit = width - 1 - column;
  return bit < 16 && (bits >> bit) & 1;
}

/* Return a new surface with the pixels of RECT of SURFACE.  RECT must
   be inside SURFACE.  */
static cairo_surface_t *
snapshot (cairo_surface_t *surface, struct rc_rect rect)
{
  cairo_surface_t *pixels
    = cairo_image_surface_create (CAIRO_FORMAT_RGB24, rect.width,
				  rect.height);
  cairo_t *cr = start_copy (pixels, surface, -rect.x, -rect.y);
  cairo_paint (cr);
  cairo_destroy (cr);
  return pixels;
}

/* Return a new surface of WIDTH, HEIGHT with the pixels of OLD at the
   same positions, or with all pixels 0 if OLD is NULL.  A pixel outside
   OLD is 0.  The copy fills only the rectangle of OLD: at the width
   RC_MAX_FRAME_SIZE, a cairo_paint of the whole surface loses pixels.
   Return NULL if the size is above RC_MAX_FRAME_SIZE, or
   Cairo cannot make the surface.  */
static cairo_surface_t *
resized_copy (cairo_surface_t *old, uint32_t width, uint32_t height)
{
  if (width > RC_MAX_FRAME_SIZE || height > RC_MAX_FRAME_SIZE)
    return NULL;
  cairo_surface_t *surface
    = cairo_image_surface_create (CAIRO_FORMAT_RGB24, width, height);
  if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (surface);
      return NULL;
    }
  if (old)
    {
      cairo_t *cr = start_copy (surface, old, 0, 0);
      fill_rect (cr, (struct rc_rect) { 0, 0,
					cairo_image_surface_get_width (old),
					cairo_image_surface_get_height (old) });
      cairo_destroy (cr);
    }
  return surface;
}

/* Return the frame ID of RENDERER, or NULL if it does not exist.  */
static struct rc_frame *
find_frame (struct rc_renderer *renderer, uint32_t id)
{
  for (int i = 0; i < renderer->nframes; i++)
    if (renderer->frames[i].id == id)
      return &renderer->frames[i];
  return NULL;
}

/* Add a frame ID with no surface to RENDERER, and return it.  */
static struct rc_frame *
add_frame (struct rc_renderer *renderer, uint32_t id)
{
  if (renderer->nframes == renderer->size)
    {
      renderer->size = renderer->size ? 2 * renderer->size : 4;
      renderer->frames = realloc (renderer->frames,
				  renderer->size * sizeof *renderer->frames);
      if (!renderer->frames)
	abort ();
    }
  struct rc_frame *frame = &renderer->frames[renderer->nframes++];
  *frame = (struct rc_frame) { id, NULL };
  return frame;
}

/* Return a Cairo context for SURFACE that fills exact pixels with
   COLOR.  */
static cairo_t *
start_draw (cairo_surface_t *surface, uint32_t color)
{
  cairo_t *cr = cairo_create (surface);
  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  set_color (cr, color);
  return cr;
}

/* Return a Cairo context for TARGET that copies the pixels of SOURCE,
   with the top left corner of SOURCE at X, Y.  A pixel outside SOURCE
   is 0.  */
static cairo_t *
start_copy (cairo_surface_t *target, cairo_surface_t *source, int64_t x,
	    int64_t y)
{
  cairo_t *cr = cairo_create (target);
  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface (cr, source, x, y);
  return cr;
}

/* Set the source of CR to COLOR, 0xRRGGBB.  The high 8 bits are not
   part of the color.  */
static void
set_color (cairo_t *cr, uint32_t color)
{
  cairo_set_source_rgb (cr, ((color >> 16) & 0xff) / 255.0,
			((color >> 8) & 0xff) / 255.0,
			(color & 0xff) / 255.0);
}

/* Fill the part of RECT inside the target of CR with the source of
   CR.  */
static void
fill_rect (cairo_t *cr, struct rc_rect rect)
{
  rect = clip_rect (rect, cairo_get_target (cr));
  if (rect.width == 0 || rect.height == 0)
    return;
  cairo_rectangle (cr, rect.x, rect.y, rect.width, rect.height);
  cairo_fill (cr);
}

/* Return the part of RECT inside SURFACE.  The result has the width
   and the height 0 if no part is inside.  */
static struct rc_rect
clip_rect (struct rc_rect rect, cairo_surface_t *surface)
{
  struct rc_rect bounds = { 0, 0, cairo_image_surface_get_width (surface),
			    cairo_image_surface_get_height (surface) };
  return intersection (rect, bounds);
}

/* Return the intersection of A and B.  */
static struct rc_rect
intersection (struct rc_rect a, struct rc_rect b)
{
  int64_t x = a.x > b.x ? a.x : b.x;
  int64_t y = a.y > b.y ? a.y : b.y;
  int64_t right = (a.x + a.width < b.x + b.width
		   ? a.x + a.width : b.x + b.width);
  int64_t bottom = (a.y + a.height < b.y + b.height
		    ? a.y + a.height : b.y + b.height);
  if (right <= x || bottom <= y)
    return (struct rc_rect) { x, y, 0, 0 };
  return (struct rc_rect) { x, y, right - x, bottom - y };
}
