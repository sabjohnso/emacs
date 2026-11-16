/* The images of a remote display session.

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

/* Emacs decodes each image.  The session sends the pixels of an image
   to its client as a PNG file in image-data messages (rdproto.h), and
   the client keeps the pixels with the ID of the image.  A draw-image
   message then tells the client to paint them.

   An image gets its ID at its first draw.  The ID is unique for the
   life of the process.  A memo in the Cairo user data of the source
   surface of the image keeps the ID, so the ID lives as long as the
   pixels, and struct image does not change.  The memo also has the
   attach that has the pixels: each attach of a client gets a number
   that is unique for the life of the process, so a new client, or a
   client of another session, gets the pixels again.

   A PNG file can have megabytes, and the draws go on the same
   connection.  Thus the draw of an image writes image data only while
   the sink has fewer than REMOTE_PACED_QUEUE_LIMIT bytes that it did
   not send.  The draw does not flush the sink: the flush at the end of
   the update sends the data with the draws, so no part of an update
   goes out alone.  Outside an update, remote_send_image_data sends the
   rest at the pace of the connection.  The files go out in order, one
   file after the other.  If a draw-image message went out before the
   data of its image was complete, the client did not paint the image.
   Thus, when no file is left, the session draws its frames again.  */

#include <config.h>

#include <stdlib.h>

#include "lisp.h"
#include "dispextern.h"
#include "rdchunk.h"
#include "rdpng.h"
#include "remoteterm.h"

/* The PNG file of an image that the client did not get complete: the
   SIZE bytes BYTES of the image ID, and the INDEX of the next
   image-data message.  NEXT is the next transfer of the session.  */
struct remote_image_transfer
{
  struct remote_image_transfer *next;
  uint32_t id;
  unsigned char *bytes;
  size_t size;
  uint32_t index;
};

/* The memo of an image, in the Cairo user data of its source surface:
   the ID of the image, and the attach whose client has its pixels, or
   0.  */
struct remote_image_memo
{
  uint32_t id;
  uintmax_t attach;
};

static struct remote_image_memo *remote_image_memo (struct image *);
static bool remote_image_fits_p (struct image *);
static bool remote_queue_image (struct remote_display_info *,
				struct image *, struct remote_image_memo *);
static unsigned char *remote_image_png (struct image *, size_t *);
static void remote_write_image_data (struct remote_display_info *);
static bool remote_send_image_step (struct remote_display_info *);
static bool remote_send_image_chunk (struct remote_display_info *);
static bool remote_transfer_pending_p (struct remote_display_info *,
				       uint32_t);
static void remote_pop_image_transfer (struct remote_display_info *);

/* The key of the memo in the user data of a surface.  */
static const cairo_user_data_key_t remote_image_key;

/* The last image ID, and the number of the last attach.  */
static uint32_t remote_last_image_id;
static uintmax_t remote_last_attach;

/* Return the ID of the image IMG for a draw on the frame F, and write
   the data of IMG for the client of F if the client does not have it.
   The caller must send a draw-image message with the ID.  If the data
   is not complete before that message, the session draws its frames
   again after the transfer.  Return 0 if F has no client, or if the
   protocol cannot send IMG: IMG did not load, or it has more than
   RD_MAX_IMAGE_SIDE pixels on a side, or more than RD_MAX_IMAGE_SIZE
   bytes of PNG.  */
uint32_t
remote_image_prepare (struct frame *f, struct image *img)
{
  struct remote_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  if (!remote_frame_has_client (f) || !remote_image_fits_p (img))
    return 0;
  struct remote_image_memo *memo = remote_image_memo (img);
  if (!memo)
    return 0;
  if (memo->attach != dpyinfo->image_attach
      && !remote_queue_image (dpyinfo, img, memo))
    return 0;
  remote_write_image_data (dpyinfo);
  if (remote_transfer_pending_p (dpyinfo, memo->id))
    dpyinfo->image_drawn_early = true;
  return memo->id;
}

/* Send image data to the client of DPYINFO at the pace of the
   connection (remote_send_paced).  Call this only outside an update:
   it flushes the sink.  */
void
remote_send_image_data (struct remote_display_info *dpyinfo)
{
  remote_send_paced (dpyinfo, remote_send_image_step);
}

/* Forget the images that the client of DPYINFO has, and the transfers
   to it.  A new client of DPYINFO, or none, comes after this call.  */
void
remote_reset_images (struct remote_display_info *dpyinfo)
{
  while (dpyinfo->image_transfers)
    remote_pop_image_transfer (dpyinfo);
  dpyinfo->image_drawn_early = false;
  dpyinfo->image_attach = ++remote_last_attach;
}

/* Return the memo of the image IMG, and make one if IMG has none.
   Return NULL if IMG has no source surface, or if there is no
   memory for the memo.  */
static struct remote_image_memo *
remote_image_memo (struct image *img)
{
  cairo_surface_t *surface;
  if (!img->cr_data
      || cairo_pattern_get_surface (img->cr_data, &surface)
	 != CAIRO_STATUS_SUCCESS)
    return NULL;
  struct remote_image_memo *memo
    = cairo_surface_get_user_data (surface, &remote_image_key);
  if (memo)
    return memo;
  memo = xmalloc (sizeof *memo);
  *memo = (struct remote_image_memo) { .id = ++remote_last_image_id };
  if (cairo_surface_set_user_data (surface, &remote_image_key, memo, xfree)
      != CAIRO_STATUS_SUCCESS)
    {
      xfree (memo);
      return NULL;
    }
  return memo;
}

/* Return true if each side of the image IMG has from 1 to
   RD_MAX_IMAGE_SIDE pixels.  */
static bool
remote_image_fits_p (struct image *img)
{
  return (0 < img->width && img->width <= RD_MAX_IMAGE_SIDE
	  && 0 < img->height && img->height <= RD_MAX_IMAGE_SIDE);
}

/* Put the PNG file of the image IMG with the memo MEMO at the end of
   the transfers of DPYINFO, and mark that the client of DPYINFO has
   it.  Return false if the encoder fails, or if the file is too
   large.  */
static bool
remote_queue_image (struct remote_display_info *dpyinfo, struct image *img,
		    struct remote_image_memo *memo)
{
  size_t size;
  unsigned char *png = remote_image_png (img, &size);
  if (!png)
    return false;
  if (size > RD_MAX_IMAGE_SIZE)
    {
      free (png);
      return false;
    }
  struct remote_image_transfer *transfer = xmalloc (sizeof *transfer);
  *transfer = (struct remote_image_transfer) { .id = memo->id,
					       .bytes = png, .size = size };
  struct remote_image_transfer **last = &dpyinfo->image_transfers;
  while (*last)
    last = &(*last)->next;
  *last = transfer;
  memo->attach = dpyinfo->image_attach;
  return true;
}

/* Return the PNG file of the pixels of the image IMG, and set *SIZE
   to its number of bytes, as rd_png_encode does.  The pixels have the
   transformation of IMG (its scale, rotation, and crop), as in
   pgtk_cr_draw_image.  */
static unsigned char *
remote_image_png (struct image *img, size_t *size)
{
  cairo_surface_t *pixels
    = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, img->width,
				  img->height);
  cairo_t *cr = cairo_create (pixels);
  cairo_set_source (cr, img->cr_data);
  cairo_paint (cr);
  cairo_destroy (cr);
  unsigned char *png = rd_png_encode (pixels, size);
  cairo_surface_destroy (pixels);
  return png;
}

/* Write the image data of DPYINFO while its sink has fewer than
   REMOTE_PACED_QUEUE_LIMIT bytes that it did not send.  Do not flush
   the sink: this runs in an update.  */
static void
remote_write_image_data (struct remote_display_info *dpyinfo)
{
  while (remote_queued (dpyinfo) < REMOTE_PACED_QUEUE_LIMIT
	 && remote_send_image_chunk (dpyinfo))
    ;
}

/* Send the next image-data message of DPYINFO, and return true.
   Return false if no transfer is left.  Then, if a draw-image message
   went out before the data of its image, draw the frames again.  */
static bool
remote_send_image_step (struct remote_display_info *dpyinfo)
{
  if (remote_send_image_chunk (dpyinfo))
    return true;
  if (dpyinfo->image_drawn_early)
    {
      dpyinfo->image_drawn_early = false;
      remote_redraw_frames (dpyinfo);
    }
  return false;
}

/* Send the next image-data message of the first transfer of DPYINFO,
   and return true.  Return false if DPYINFO has no transfer.  */
static bool
remote_send_image_chunk (struct remote_display_info *dpyinfo)
{
  struct remote_image_transfer *transfer = dpyinfo->image_transfers;
  if (!transfer)
    return false;
  struct rd_message message;
  rd_chunk_message (RD_IMAGE_DATA, transfer->id, transfer->bytes,
		    transfer->size, transfer->index, &message);
  remote_send_message (dpyinfo, &message);
  if (++transfer->index == rd_chunk_count (transfer->size))
    remote_pop_image_transfer (dpyinfo);
  return true;
}

/* Return true if DPYINFO has a transfer of the image ID.  */
static bool
remote_transfer_pending_p (struct remote_display_info *dpyinfo, uint32_t id)
{
  for (struct remote_image_transfer *transfer = dpyinfo->image_transfers;
       transfer; transfer = transfer->next)
    if (transfer->id == id)
      return true;
  return false;
}

/* Remove the first transfer of DPYINFO, and free its file.  */
static void
remote_pop_image_transfer (struct remote_display_info *dpyinfo)
{
  struct remote_image_transfer *transfer = dpyinfo->image_transfers;
  dpyinfo->image_transfers = transfer->next;
  free (transfer->bytes);
  xfree (transfer);
}
