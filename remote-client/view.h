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

#include "render.h"

struct rc_view;

/* Return a new view of the frames of RENDERER.  When the user closes a
   window, the view calls ON_CLOSE with DATA.  */
extern struct rc_view *rc_view_new (struct rc_renderer *renderer,
				    void (*on_close) (void *data),
				    void *data);
extern void rc_view_free (struct rc_view *view);

/* Update VIEW for CHANGE, a change of its renderer.  Make, resize, or
   remove the window of a frame, or draw it again.  Call this function
   after each rc_apply of the renderer, before the next one.  */
extern void rc_view_update (struct rc_view *view, struct rc_change change);

#endif /* REMOTE_CLIENT_VIEW_H */
