/* The popup menus of the remote display client.

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

/* The server sends the entries of a popup menu in menu-item messages,
   and then a show-menu message.  A struct rc_menu_model keeps the
   entries of one menu until its show-menu message comes, and then
   makes a tree of them.  The view shows the tree.  The model needs
   no display.

   The entries of a menu nest with the submenu-start and the
   submenu-end kinds.  A show-menu message gives no tree when the
   entries of its serial do not nest correctly.  Then the view answers
   with a dismissal.  */

#ifndef REMOTE_CLIENT_MENU_H
#define REMOTE_CLIENT_MENU_H

#include <stddef.h>
#include <stdint.h>

#include "rdproto.h"

/* The deepest nest of submenus that a tree can have.  Entries with a
   deeper nest give no tree.  */
enum { RC_MENU_MAX_DEPTH = 64 };

/* One node of a menu tree.  KIND, FLAGS and ID are the fields of its
   entry (enum rd_menu_kind and enum rd_menu_flag).  LABEL has the
   NLABEL bytes of the label of the entry and a NUL byte after them.
   KEY has the NKEY bytes of the key equivalent and a NUL byte after
   them.  A node of the kind RD_MENU_KIND_SUBMENU_START has the
   NCHILDREN nodes CHILDREN, the entries before the submenu-end entry
   that closes it.  A node of another kind has no children.  The tree
   has no node for a submenu-end entry.  */
struct rc_menu_node
{
  uint8_t kind;
  uint8_t flags;
  uint32_t id;
  char *label;
  size_t nlabel;
  char *key;
  size_t nkey;
  struct rc_menu_node *children;
  size_t nchildren;
};

/* The tree of the menu SERIAL.  The NCHILDREN nodes CHILDREN are its
   top entries.  */
struct rc_menu
{
  uint32_t serial;
  struct rc_menu_node *children;
  size_t nchildren;
};

/* The entries of the menu that has no show-menu message yet.  Only
   the functions of menu.c use the fields.  Zero bytes make an empty
   model.  */
struct rc_menu_model
{
  uint32_t serial;
  struct rc_menu_node *entries;
  size_t nentries;
  size_t size;
};

/* Add the entry ITEM to MODEL.  If ITEM has another serial than the
   entries of MODEL, MODEL first drops these entries.  */
extern void rc_menu_add (struct rc_menu_model *model,
			 const struct rd_menu_item *item);

/* Take the entries of the menu SERIAL from MODEL, and return their
   tree.  Return NULL if MODEL has no entries of SERIAL, or if they do
   not nest correctly.  The caller frees the tree with
   rc_menu_free.  */
extern struct rc_menu *rc_menu_show (struct rc_menu_model *model,
				     uint32_t serial);

/* Drop the entries of the menu SERIAL from MODEL.  Keep the entries
   of another serial.  */
extern void rc_menu_hide (struct rc_menu_model *model, uint32_t serial);

/* Drop all entries of MODEL.  */
extern void rc_menu_model_free (struct rc_menu_model *model);

/* Free the tree MENU.  MENU can be NULL.  */
extern void rc_menu_free (struct rc_menu *menu);

#endif /* REMOTE_CLIENT_MENU_H */
