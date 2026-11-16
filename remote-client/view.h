/* The GTK view of the remote display client.

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

/* A view shows each root frame of a renderer in a GTK window of its
   own.  A frame is a root if its place (rc_view_place) has no parent,
   or if it has no place.  The window of a root shows the surface of
   the root and the surfaces of its visible child frames (compose.h).
   A child frame has no window.  The pointer input of a window goes to
   the frame under the pointer, in the coordinates of that frame.
   After a press, the pointer input goes to the frame of the press
   until the release of the last button, as with the implicit grab of
   X.  The key and focus input goes to the root.  The program must call
   gtk_init before it makes a view.  */

#ifndef REMOTE_CLIENT_VIEW_H
#define REMOTE_CLIENT_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "menu.h"
#include "rdproto.h"
#include "render.h"

struct rc_view;

/* The functions of a view.  The view calls ON_CLOSE with the frame of
   a window when the user asks to close the window, and SEND with each
   message of the input of the user.  It calls CLIPBOARD with the UTF-8
   text of the clipboard of the desktop after each change of the
   clipboard, and after rc_view_request_clipboard; the text is NULL if
   the clipboard has no text.  CLIPBOARD can be NULL: then the view
   does not watch the clipboard.  DATA is their first argument.  The
   view keeps the window after ON_CLOSE.  It removes a window only in
   rc_view_update, when the renderer removes the frame.  */
struct rc_view_functions
{
  void (*on_close) (void *data, uint32_t frame);
  void (*send) (void *data, const struct rd_message *message);
  void (*clipboard) (void *data, const char *text);
  void *data;
};

/* Return a new view of the frames of RENDERER, with FUNCTIONS.  */
extern struct rc_view *rc_view_new (struct rc_renderer *renderer,
				    struct rc_view_functions functions);
extern void rc_view_free (struct rc_view *view);

/* Update VIEW for CHANGE, a change of its renderer.  Make, resize, or
   remove the window of a frame, or draw it again.  Call this function
   after each rc_apply of the renderer, before the next one.  */
extern void rc_view_update (struct rc_view *view, struct rc_change change);

/* Keep the place PLACE of the frame PLACE->frame in VIEW, from a
   place-frame message.  Remove the window of a frame that gets a
   parent.  Make a window for a defined frame that becomes a root, and
   hide or show the window of a root.  Draw again the windows that
   show the frame before and after the change.  */
extern void rc_view_place (struct rc_view *view,
			   const struct rd_place_frame *place);

/* Forget all the places of VIEW.  Use this for a new connection,
   before its first message.  A child frame that the new connection
   does not place again is not shown.  */
extern void rc_view_reset_places (struct rc_view *view);

/* Show in the title of each window of VIEW whether the client has a
   connection to its server: CONNECTED.  A new view is connected.  */
extern void rc_view_set_connected (struct rc_view *view, bool connected);

/* Put the SIZE bytes TEXT, UTF-8, on the clipboard of the desktop.
   Later, the view calls the CLIPBOARD function of VIEW with TEXT.  It
   does not give it a text of a request from before this call.  */
extern void rc_view_set_clipboard (struct rc_view *view, const char *text,
				   size_t size);

/* Ask for the text of the clipboard of the desktop.  The view gives it
   to the CLIPBOARD function of VIEW later.  */
extern void rc_view_request_clipboard (struct rc_view *view);

/* Show the popup menu MENU of the show-menu message SHOW in the window
   of the root of its frame, at the point of SHOW in the frame, with
   the title of SHOW.  MENU is the tree of the entries of SHOW
   (rc_menu_show), or NULL if the entries are bad.  VIEW does not keep
   MENU.  First close the menu that is open, with no choice.  Later,
   the view sends one menu-choice message for SHOW: the item that the
   user selects, or a dismissal (CHOSEN 0) if the menu closes with no
   selection.  If MENU is NULL or no window shows the frame, the view
   sends the dismissal at once.  The menu stays open when its frame
   moves or gets a new root, and closes with a dismissal when its
   frame or the window of the menu goes away.  */
extern void rc_view_show_menu (struct rc_view *view,
			       const struct rd_show_menu *show,
			       const struct rc_menu *menu);

/* Close the menu SERIAL of VIEW, if it is open.  Then the view sends
   no choice for SERIAL.  */
extern void rc_view_hide_menu (struct rc_view *view, uint32_t serial);

/* Close the open menu of VIEW, if it has one.  Then the view sends no
   choice for it.  Call this function when the connection of the menu
   ends.  */
extern void rc_view_close_menu (struct rc_view *view);

/* Return the number of windows of VIEW.  */
extern int rc_view_window_count (const struct rc_view *view);

#endif /* REMOTE_CLIENT_VIEW_H */
