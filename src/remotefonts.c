/* The font files that a remote display session sends to its client.

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

/* A client that has no file for a define-font message sends a
   want-font message with the hash of the file.  The session sends
   only a file of a font that a session uses, so a client cannot read
   other files.  It sends the file in font-data messages, in order.

   A font file can have many megabytes, and the draws of the session go
   on the same connection.  Thus the session sends font data only
   while its sink has fewer than REMOTE_FONT_QUEUE_LIMIT bytes that it
   did not send.  The transport calls remote_send_font_data when its
   queue is empty again.

   After the last font-data message of a file, the session forgets
   that the client has the definitions of the fonts of the file.  When
   no transfer is left, it draws each frame again, so it defines these
   fonts again.  The messages keep their order, so the client has the
   file before the new definitions.  One redraw serves all the files of
   a burst of requests.  A transfer belongs to one client: an attach or a
   detach drops each transfer.  */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "lisp.h"
#include "coding.h"
#include "rdproto.h"
#include "remoteterm.h"

/* The session sends font data while its sink has fewer bytes than
   this that it did not send.  */
enum { REMOTE_FONT_QUEUE_LIMIT = 64 * 1024 };

/* A font file that the client asked for: its HASH, its file descriptor
   FD, its TOTAL number of bytes, and the OFFSET of the next bytes to
   send.  NEXT is the next transfer of the session.  */
struct remote_font_transfer
{
  struct remote_font_transfer *next;
  unsigned char hash[RD_HASH_SIZE];
  int fd;
  uint32_t total;
  uint32_t offset;
};

static struct remote_font_transfer *remote_find_transfer
  (struct remote_display_info *, const unsigned char *);
static int remote_open_font_file (const unsigned char *, uint32_t *);
static bool remote_send_chunk (struct remote_display_info *,
			       struct remote_font_transfer *);
static void remote_pop_transfer (struct remote_display_info *);

/* The function of a want-font MESSAGE from the client of DPYINFO.
   Start to send the file, if a font of a session has its hash and the
   file did not change.  Ignore a request for a file that the session
   sends now, and a short hash: the decoder does not set the bytes of
   the hash after it.  */
void
remote_want_font (struct remote_display_info *dpyinfo,
		  const struct rd_message *message)
{
  const struct rd_want_font *want = &message->u.want_font;
  if (want->nhash != RD_HASH_SIZE
      || remote_find_transfer (dpyinfo, want->hash))
    return;
  uint32_t total;
  int fd = remote_open_font_file (want->hash, &total);
  if (fd < 0)
    return;
  struct remote_font_transfer *transfer = xmalloc (sizeof *transfer);
  *transfer = (struct remote_font_transfer) { .fd = fd, .total = total };
  memcpy (transfer->hash, want->hash, RD_HASH_SIZE);
  struct remote_font_transfer **last = &dpyinfo->font_transfers;
  while (*last)
    last = &(*last)->next;
  *last = transfer;
  remote_send_font_data (dpyinfo);
}

/* Send font data to the client of DPYINFO until its sink has
   REMOTE_FONT_QUEUE_LIMIT bytes that it did not send, or no transfer
   is left.  Flush the sink each time: if the connection takes all the
   bytes, the transport does not call this function again.  After the
   last transfer, draw the frames again.  */
void
remote_send_font_data (struct remote_display_info *dpyinfo)
{
  while (dpyinfo->font_transfers)
    {
      while (dpyinfo->font_transfers
	     && remote_queued (dpyinfo) < REMOTE_FONT_QUEUE_LIMIT)
	{
	  struct remote_font_transfer *transfer = dpyinfo->font_transfers;
	  if (!remote_send_chunk (dpyinfo, transfer))
	    /* The file changed, or cannot be read now.  */
	    remote_pop_transfer (dpyinfo);
	  else if (transfer->offset == transfer->total)
	    {
	      remote_forget_fonts (dpyinfo, transfer->hash);
	      remote_pop_transfer (dpyinfo);
	      if (!dpyinfo->font_transfers)
		remote_redraw_frames (dpyinfo);
	    }
	}
      remote_flush (dpyinfo);
      if (remote_queued (dpyinfo) > 0)
	return;
    }
}

/* Forget each transfer of DPYINFO, and close its file.  */
void
remote_drop_font_transfers (struct remote_display_info *dpyinfo)
{
  while (dpyinfo->font_transfers)
    remote_pop_transfer (dpyinfo);
}

/* Return the transfer of DPYINFO with the hash HASH, or NULL.  */
static struct remote_font_transfer *
remote_find_transfer (struct remote_display_info *dpyinfo,
		      const unsigned char *hash)
{
  for (struct remote_font_transfer *transfer = dpyinfo->font_transfers;
       transfer; transfer = transfer->next)
    if (!memcmp (transfer->hash, hash, RD_HASH_SIZE))
      return transfer;
  return NULL;
}

/* Open the file of a font with the hash HASH, and put the size of the
   file at the time of its hash in *TOTAL.  Return the file descriptor,
   or -1 if no font has the hash or the file cannot be read.  A file
   that became shorter ends its transfer at the first short read: see
   remote_send_chunk.  The client checks the hash of the bytes.  */
static int
remote_open_font_file (const unsigned char *hash, uint32_t *total)
{
  Lisp_Object file = remote_font_file (hash, total);
  if (!STRINGP (file))
    return -1;
  return emacs_open (SSDATA (ENCODE_FILE (file)), O_RDONLY, 0);
}

/* Send the next font-data message of TRANSFER to the client of
   DPYINFO.  Return false if the file has fewer bytes than its
   total.  */
static bool
remote_send_chunk (struct remote_display_info *dpyinfo,
		   struct remote_font_transfer *transfer)
{
  struct rd_message message = { .type = RD_FONT_DATA };
  struct rd_font_data *data = &message.u.font_data;
  uint32_t size = min (transfer->total - transfer->offset,
		       RD_MAX_FONT_CHUNK);
  ssize_t n;
  do
    n = pread (transfer->fd, data->bytes, size, transfer->offset);
  while (n < 0 && errno == EINTR);
  if (n != size)
    return false;
  memcpy (data->hash, transfer->hash, RD_HASH_SIZE);
  data->nhash = RD_HASH_SIZE;
  data->offset = transfer->offset;
  data->total = transfer->total;
  data->nbytes = size;
  remote_send_message (dpyinfo, &message);
  transfer->offset += size;
  return true;
}

/* Remove the first transfer of DPYINFO, and close its file.  */
static void
remote_pop_transfer (struct remote_display_info *dpyinfo)
{
  struct remote_font_transfer *transfer = dpyinfo->font_transfers;
  dpyinfo->font_transfers = transfer->next;
  emacs_close (transfer->fd);
  xfree (transfer);
}
