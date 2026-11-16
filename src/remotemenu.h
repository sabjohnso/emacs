/* Popup menus of a remote display session.

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

#ifndef EMACS_REMOTEMENU_H
#define EMACS_REMOTEMENU_H

#include <stdint.h>

#include "lisp.h"

struct frame;
struct rd_message;
struct remote_display_info;
struct remote_menu_wait;

/* The popup menu state of a session.  */
struct remote_menu_state
{
  /* The serial of the last menu that the session showed.  */
  uint32_t serial;

  /* The menu that the session waits for, or NULL.  See
     remotemenu.c.  */
  struct remote_menu_wait *wait;
};

/* The menu_show_hook of a session.  Show the menu of menu_items on
   the client of F at the pixel position X, Y of F, with the title
   TITLE, wait for the answer of the client, and return the selection.
   MENUFLAGS and ERROR_NAME are as for menu_show_hook.  */
extern Lisp_Object remote_menu_show (struct frame *f, int x, int y,
				     int menuflags, Lisp_Object title,
				     const char **error_name);

/* The function of a menu-choice MESSAGE from the client of
   DPYINFO.  */
extern void remote_menu_choice_input (struct remote_display_info *dpyinfo,
				      const struct rd_message *message);

/* End the wait for the menu of DPYINFO as a dismissal, if the session
   waits for a menu.  Call this before the client of DPYINFO changes,
   and before DPYINFO is freed.  */
extern void remote_drop_menu (struct remote_display_info *dpyinfo);

#endif /* EMACS_REMOTEMENU_H */
