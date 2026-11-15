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

/* A view shows each frame of a renderer in a GTK window of its own.
   The window of a frame shows the surface of the frame.  The program
   must call gtk_init before it makes a view.  */

#ifndef REMOTE_CLIENT_VIEW_H
#define REMOTE_CLIENT_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "rdproto.h"
#include "render.h"

struct rc_view;

/* The functions of a view.  The view calls ON_CLOSE with the frame of
   a window when the user asks to close the window, and SEND with each
   message of the input of the user.  DATA is their first argument.
   The view keeps the window after ON_CLOSE.  It removes a window only
   in rc_view_update, when the renderer removes the frame.  */
struct rc_view_functions
{
  void (*on_close) (void *data, uint32_t frame);
  void (*send) (void *data, const struct rd_message *message);
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

/* Show in the title of each window of VIEW whether the client has a
   connection to its server: CONNECTED.  A new view is connected.  */
extern void rc_view_set_connected (struct rc_view *view, bool connected);

/* Return the number of windows of VIEW.  */
extern int rc_view_window_count (const struct rc_view *view);

#endif /* REMOTE_CLIENT_VIEW_H */
