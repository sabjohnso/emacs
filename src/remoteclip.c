/* The clipboard of a remote display session.

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

/* The client sends the text of its clipboard each time the clipboard
   changes, so a yank in Emacs needs no round trip.  This file joins the
   clipboard-data messages of the client (rdchunk.h), and puts the text
   in the terminal parameter `remote-clipboard' of the session.  The
   terminal parameter `remote-clipboard-owned' is nil then: Emacs does
   not own the text.  lisp/term/remote-win.el reads these parameters for
   the selections of Emacs, and sends a kill to the client with
   `remote-display--send-clipboard'.

   A kill can have megabytes, so the session sends it at the pace of
   the connection, as the font data (REMOTE_PACED_QUEUE_LIMIT).  A new
   kill replaces the rest of the text before it.  */

#include <config.h>

#include "lisp.h"
#include "coding.h"
#include "rdchunk.h"
#include "remoteterm.h"

static bool remote_send_clipboard_step (struct remote_display_info *);

/* The function of a clipboard-data MESSAGE from the client of DPYINFO.
   After the last message of a text, make the text the clipboard of the
   session.  */
void
remote_clipboard_input (struct remote_display_info *dpyinfo,
			const struct rd_message *message)
{
  struct rd_chunks *chunks = &dpyinfo->clipboard;
  if (rd_chunks_add (chunks, &message->u.clipboard_data) != RD_CHUNK_DONE)
    return;
  Lisp_Object terminal;
  XSETTERMINAL (terminal, dpyinfo->terminal);
  Fset_terminal_parameter (terminal, Qremote_clipboard,
			   make_string_from_utf8 ((char *) chunks->bytes,
						  chunks->total));
  Fset_terminal_parameter (terminal, Qremote_clipboard_owned, Qnil);
}

/* Forget the clipboard text that comes from the client of DPYINFO,
   and the rest of the text that goes to it.  */
void
remote_drop_clipboard (struct remote_display_info *dpyinfo)
{
  rd_chunks_free (&dpyinfo->clipboard);
  rd_chunk_queue_clear (&dpyinfo->clipboard_out);
}

/* Send clipboard data to the client of DPYINFO at the pace of the
   connection (remote_send_paced).  */
void
remote_send_clipboard_data (struct remote_display_info *dpyinfo)
{
  remote_send_paced (dpyinfo, remote_send_clipboard_step);
}

/* Send the next clipboard-data message of DPYINFO, and return true.
   Return false if the text ended.  */
static bool
remote_send_clipboard_step (struct remote_display_info *dpyinfo)
{
  struct rd_message message;
  if (!rd_chunk_queue_next (&dpyinfo->clipboard_out, &message))
    return false;
  remote_send_message (dpyinfo, &message);
  return true;
}

DEFUN ("remote-display--send-clipboard", Fremote_display__send_clipboard,
       Sremote_display__send_clipboard, 2, 2, 0,
       doc: /* Send TEXT to the clipboard of the client of the session DISPLAY.
The session sends the text at the pace of the connection, and the text
replaces the rest of a text before it.  A session with no client sends
nothing.  Return nil.  Signal an error if the UTF-8 bytes of TEXT are
too many for the protocol: `remote-display-clipboard-too-large'.  */)
  (Lisp_Object display, Lisp_Object text)
{
  struct remote_display_info *dpyinfo = check_x_display_info (display);
  CHECK_STRING (text);
  Lisp_Object bytes = ENCODE_UTF_8 (text);
  if (SBYTES (bytes) > RD_MAX_CLIPBOARD_SIZE)
    xsignal1 (Qremote_display_clipboard_too_large,
	      make_int (SBYTES (bytes)));
  if (!rd_chunk_queue_set (&dpyinfo->clipboard_out, SDATA (bytes),
			   SBYTES (bytes)))
    memory_full (SBYTES (bytes));
  remote_send_clipboard_data (dpyinfo);
  return Qnil;
}

void
syms_of_remoteclip (void)
{
  DEFSYM (Qremote_clipboard, "remote-clipboard");
  DEFSYM (Qremote_clipboard_owned, "remote-clipboard-owned");
  DEFSYM (Qremote_display_clipboard_too_large,
	  "remote-display-clipboard-too-large");
  define_error (Qremote_display_clipboard_too_large,
		"The text is too large for the clipboard of the client",
		Qerror);
  defsubr (&Sremote_display__send_clipboard);
}
