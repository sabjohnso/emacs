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

   An image gets its ID at its first draw.  The ID is not 0, and it is
   unique for the life of the process until the counter wraps.  A memo
   in the Cairo user data of the source surface of the image keeps the
   ID, so the ID lives as long as the pixels, and struct image does not
   change.  The memo also has the attach that has the pixels: each
   attach of a client gets a number that is unique for the life of the
   process, so a new client gets the pixels again.  An image cache
   belongs to one terminal (share_image_cache), thus the surface of an
   image has one session, and the memo needs only one attach.

   The protocol cannot send an image with more than RD_MAX_IMAGE_SIDE
   pixels on a side, or with a PNG file of more than RD_MAX_IMAGE_SIZE
   bytes.  The session then draws the outline of the image.  The memo
   keeps the refusal of the PNG file, so the session encodes the pixels
   of an image one time, and not at each draw.

   A PNG file can have megabytes, and the draws go on the same
   connection.  Thus the draw of an image writes image data only while
   the sink has fewer than REMOTE_PACED_QUEUE_LIMIT bytes that it did
   not send.  The draw does not flush the sink: the flush at the end of
   the update sends the data with the draws, so no part of an update
   goes out alone.  Outside an update, remote_send_images sends the
   rest at the pace of the connection.  The files go out in order, one
   file after the other.  If a draw-image message went out before the
   data of its image was complete, the client did not paint the image.
   Thus, when no file is left, the session draws its frames again.

   When the pixels of an image go, Cairo calls remote_image_gone with
   the memo.  If the client of a session has the ID, the memo goes on
   the list of IDs to forget of that session.  The session sends a
   forget-image message for each of them at the end of the next update,
   or when the transport sent all its bytes.  The callback does not
   send, because it runs inside a free of Cairo, and a send can
   allocate and exit.  A canvas changes its pixels and keeps its
   surface, so a refresh of a canvas gives it a new ID and puts the old
   ID on the list.  */

#include <config.h>

#include <stdlib.h>

#include "lisp.h"
#include "dispextern.h"
#include "rdchunk.h"
#include "rdpng.h"
#include "remoteterm.h"

/* The images of the client of one session: the number of the attach
   of the client, the PNG files that the client did not get complete,
   in order, the IDs that the client must forget, and true if a
   draw-image message went out before the data of its image.  */
struct remote_images
{
  uintmax_t attach;
  struct remote_image_transfer *transfers;
  struct remote_image_memo *forgets;
  bool drawn_early;
};

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
   the ID of the image, the refresh of the image (see
   canvas_prepare_for_display) that has the pixels of the ID, the
   attach whose client has the pixels, or 0, and true if the protocol
   cannot send the PNG file of the pixels.  On the list of IDs to
   forget of a session, NEXT is the next memo of the list.  */
struct remote_image_memo
{
  struct remote_image_memo *next;
  uint32_t id;
  uint32_t refresh;
  uintmax_t attach;
  bool refused;
};

static struct remote_image_memo *remote_current_memo
  (struct remote_display_info *, struct image *);
static bool remote_offer_image (struct remote_display_info *,
				struct image *, struct remote_image_memo *);
static void remote_send_image_data (struct remote_display_info *);
static struct remote_image_memo *remote_image_memo (struct image *);
static uint32_t remote_new_image_id (void);
static void remote_renew_image_id (struct remote_images *,
				   struct remote_image_memo *, uint32_t);
static void remote_image_gone (void *);
static struct remote_images *remote_attach_images (uintmax_t);
static void remote_forget_image_id (struct remote_images *,
				    struct remote_image_memo *);
static void remote_drop_image_transfer (struct remote_images *, uint32_t);
static uint32_t remote_pop_forget (struct remote_images *);
static void remote_free_transfer_at (struct remote_image_transfer **);
static bool remote_image_fits_p (struct image *);
static bool remote_queue_image (struct remote_images *, struct image *,
				struct remote_image_memo *);
static unsigned char *remote_image_png (struct image *, size_t *);
static void remote_append_transfer (struct remote_images *,
				    struct remote_image_transfer *);
static void remote_write_image_data (struct remote_display_info *);
static bool remote_send_image_step (struct remote_display_info *);
static bool remote_send_image_chunk (struct remote_display_info *);
static bool remote_transfer_pending_p (struct remote_images *, uint32_t);
static void remote_pop_image_transfer (struct remote_images *);

/* The key of the memo in the user data of a surface.  */
static const cairo_user_data_key_t remote_image_key;

/* The state of the images of all sessions: the last image ID, the
   number of the last attach, and the largest PNG file that a session
   sends.  */
static struct
{
  uint32_t last_image_id;
  uintmax_t last_attach;
  size_t max_size;
} remote_image_counters = { .max_size = RD_MAX_IMAGE_SIZE };

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
  if (!remote_frame_has_client (f))
    return 0;
  struct remote_image_memo *memo = remote_current_memo (dpyinfo, img);
  if (!memo || !remote_offer_image (dpyinfo, img, memo))
    return 0;
  return memo->id;
}

/* Send the image messages that wait in DPYINFO: first the forget-image
   messages, then the image data at the pace of the connection
   (remote_send_paced).  Call this only outside an update: it flushes
   the sink.  */
void
remote_send_images (struct remote_display_info *dpyinfo)
{
  remote_send_image_forgets (dpyinfo);
  remote_send_image_data (dpyinfo);
}

/* Send a forget-image message for each ID on the list of IDs to
   forget of DPYINFO, and empty the list.  The messages are small, so
   they do not wait for the pace of the connection.  */
void
remote_send_image_forgets (struct remote_display_info *dpyinfo)
{
  struct remote_images *images = dpyinfo->images;
  while (images->forgets)
    {
      struct rd_message message
	= { .type = RD_FORGET_IMAGE,
	    .u.forget_image = { .image = remote_pop_forget (images) } };
      remote_send_message (dpyinfo, &message);
    }
}

/* Give the session DPYINFO the state of its images, with no client.
   Call this one time, when the session starts.  */
void
remote_init_images (struct remote_display_info *dpyinfo)
{
  dpyinfo->images = xzalloc (sizeof *dpyinfo->images);
}

/* Forget the images that the client of DPYINFO has, the transfers to
   it, and the IDs that it must forget.  A new client of DPYINFO, or
   none, comes after this call.  */
void
remote_reset_images (struct remote_display_info *dpyinfo)
{
  struct remote_images *images = dpyinfo->images;
  while (images->transfers)
    remote_pop_image_transfer (images);
  while (images->forgets)
    remote_pop_forget (images);
  images->drawn_early = false;
  images->attach = ++remote_image_counters.last_attach;
}

/* Free the state of the images of DPYINFO.  Call this one time, when
   the session ends.  */
void
remote_free_images (struct remote_display_info *dpyinfo)
{
  remote_reset_images (dpyinfo);
  xfree (dpyinfo->images);
  dpyinfo->images = NULL;
}

/* Set the last image ID to ID.  This is for the tests.  */
void
remote_set_last_image_id (uint32_t id)
{
  remote_image_counters.last_image_id = id;
}

/* Set the largest PNG file that a session sends to SIZE bytes, and
   return the limit before the call.  This is for the tests.  */
size_t
remote_set_max_image_size (size_t size)
{
  size_t old = remote_image_counters.max_size;
  remote_image_counters.max_size = size;
  return old;
}

/* Return the memo of the image IMG, with the ID of the pixels that IMG
   has now.  Return NULL if the protocol cannot send IMG.  If the ID
   changes, the client of DPYINFO forgets the old ID.  */
static struct remote_image_memo *
remote_current_memo (struct remote_display_info *dpyinfo, struct image *img)
{
  if (!remote_image_fits_p (img))
    return NULL;
  struct remote_image_memo *memo = remote_image_memo (img);
  if (!memo)
    return NULL;
  if (memo->refresh != img->refresh)
    remote_renew_image_id (dpyinfo->images, memo, img->refresh);
  return memo->refused ? NULL : memo;
}

/* Make sure that the client of DPYINFO gets the pixels of the image
   IMG with the memo MEMO, and write the image data that the sink has
   room for.  Return false if the protocol cannot send the PNG file of
   IMG.  */
static bool
remote_offer_image (struct remote_display_info *dpyinfo, struct image *img,
		    struct remote_image_memo *memo)
{
  struct remote_images *images = dpyinfo->images;
  if (memo->attach != images->attach
      && !remote_queue_image (images, img, memo))
    return false;
  remote_write_image_data (dpyinfo);
  if (remote_transfer_pending_p (images, memo->id))
    images->drawn_early = true;
  return true;
}

/* Send image data to the client of DPYINFO at the pace of the
   connection (remote_send_paced).  It flushes the sink.  */
static void
remote_send_image_data (struct remote_display_info *dpyinfo)
{
  remote_send_paced (dpyinfo, remote_send_image_step);
}

/* Return the memo of the image IMG, and make one if IMG has none.
   Return NULL if IMG has no source surface.  */
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
  *memo = (struct remote_image_memo) { .id = remote_new_image_id (),
				       .refresh = img->refresh };
  /* Cairo fails here only if it has no memory, as xmalloc does.  */
  if (cairo_surface_set_user_data (surface, &remote_image_key, memo,
				   remote_image_gone)
      != CAIRO_STATUS_SUCCESS)
    {
      xfree (memo);
      memory_full (0);
    }
  return memo;
}

/* Return a new image ID.  The ID 0 means no image, so skip it when
   the counter wraps.  */
static uint32_t
remote_new_image_id (void)
{
  uint32_t id = ++remote_image_counters.last_image_id;
  return id ? id : ++remote_image_counters.last_image_id;
}

/* Give MEMO a new ID for the pixels of the refresh REFRESH of its
   image, a canvas that changed its pixels in place.  If the client of
   IMAGES has the old ID, put the old ID on the list of IDs to forget
   of IMAGES.  */
static void
remote_renew_image_id (struct remote_images *images,
		       struct remote_image_memo *memo, uint32_t refresh)
{
  if (memo->attach == images->attach)
    {
      struct remote_image_memo *old = xmalloc (sizeof *old);
      *old = (struct remote_image_memo) { .id = memo->id };
      remote_forget_image_id (images, old);
    }
  *memo = (struct remote_image_memo) { .id = remote_new_image_id (),
				       .refresh = refresh };
}

/* The destroy function of the memo DATA: Cairo calls it when the
   source surface of the image of DATA goes.  If the client of a
   session has the ID of DATA, put DATA on the list of IDs to forget of
   that session.  Else free DATA.  This runs inside a free of Cairo:
   it must not run Lisp, allocate, or send.  */
static void
remote_image_gone (void *data)
{
  struct remote_image_memo *memo = data;
  struct remote_images *images = remote_attach_images (memo->attach);
  if (images)
    remote_forget_image_id (images, memo);
  else
    xfree (memo);
}

/* Return the images of the session whose client has the attach
   ATTACH, or NULL if no session has it.  A deleted session is not in
   x_display_list, so this does not return its images.  */
static struct remote_images *
remote_attach_images (uintmax_t attach)
{
  if (!attach)
    return NULL;
  for (struct remote_display_info *d = x_display_list; d; d = d->next)
    if (d->images->attach == attach)
      return d->images;
  return NULL;
}

/* Put MEMO on the list of IDs to forget of IMAGES, and stop the
   transfer of its ID.  IMAGES then owns MEMO.  */
static void
remote_forget_image_id (struct remote_images *images,
			struct remote_image_memo *memo)
{
  remote_drop_image_transfer (images, memo->id);
  memo->next = images->forgets;
  images->forgets = memo;
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
   the transfers of IMAGES, and mark that the client of IMAGES has it.
   If the encoder fails, or if the file is too large, mark the refusal
   in MEMO, and return false.  */
static bool
remote_queue_image (struct remote_images *images, struct image *img,
		    struct remote_image_memo *memo)
{
  size_t size;
  unsigned char *png = remote_image_png (img, &size);
  if (!png || size > remote_image_counters.max_size)
    {
      free (png);
      memo->refused = true;
      return false;
    }
  struct remote_image_transfer *transfer = xmalloc (sizeof *transfer);
  *transfer = (struct remote_image_transfer) { .id = memo->id,
					       .bytes = png, .size = size };
  remote_append_transfer (images, transfer);
  memo->attach = images->attach;
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

/* Put TRANSFER at the end of the transfers of IMAGES.  */
static void
remote_append_transfer (struct remote_images *images,
			struct remote_image_transfer *transfer)
{
  struct remote_image_transfer **last = &images->transfers;
  while (*last)
    last = &(*last)->next;
  *last = transfer;
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
  if (dpyinfo->images->drawn_early)
    {
      dpyinfo->images->drawn_early = false;
      remote_redraw_frames (dpyinfo);
    }
  return false;
}

/* Send the next image-data message of the first transfer of DPYINFO,
   and return true.  Return false if DPYINFO has no transfer.  */
static bool
remote_send_image_chunk (struct remote_display_info *dpyinfo)
{
  struct remote_image_transfer *transfer = dpyinfo->images->transfers;
  if (!transfer)
    return false;
  struct rd_message message;
  rd_chunk_message (RD_IMAGE_DATA, transfer->id, transfer->bytes,
		    transfer->size, transfer->index, &message);
  remote_send_message (dpyinfo, &message);
  if (++transfer->index == rd_chunk_count (transfer->size))
    remote_pop_image_transfer (dpyinfo->images);
  return true;
}

/* Return true if IMAGES has a transfer of the image ID.  */
static bool
remote_transfer_pending_p (struct remote_images *images, uint32_t id)
{
  for (struct remote_image_transfer *transfer = images->transfers;
       transfer; transfer = transfer->next)
    if (transfer->id == id)
      return true;
  return false;
}

/* Remove the first transfer of IMAGES, and free its file.  */
static void
remote_pop_image_transfer (struct remote_images *images)
{
  remote_free_transfer_at (&images->transfers);
}

/* Remove the transfer of the image ID from the transfers of IMAGES, if
   it is there, and free its file.  */
static void
remote_drop_image_transfer (struct remote_images *images, uint32_t id)
{
  struct remote_image_transfer **link = &images->transfers;
  while (*link && (*link)->id != id)
    link = &(*link)->next;
  if (*link)
    remote_free_transfer_at (link);
}

/* Remove the transfer at LINK from its list, and free it and its
   file.  */
static void
remote_free_transfer_at (struct remote_image_transfer **link)
{
  struct remote_image_transfer *transfer = *link;
  *link = transfer->next;
  free (transfer->bytes);
  xfree (transfer);
}

/* Remove the first memo of the list of IDs to forget of IMAGES, free
   it, and return its ID.  */
static uint32_t
remote_pop_forget (struct remote_images *images)
{
  struct remote_image_memo *memo = images->forgets;
  uint32_t id = memo->id;
  images->forgets = memo->next;
  xfree (memo);
  return id;
}
