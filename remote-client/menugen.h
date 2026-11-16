/* Random menus for the tests of the menus of the client.

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


/* A random menu is a sequence of entries that nest correctly, with at
   least one entry.  Its items, separators and submenus have random
   flags, labels and keys.  Its submenu-end entries have only the
   kind.  The generator is splitmix64: a seed gives the same menu each
   time.  The tests use these menus, and the program does not.  */

#ifndef REMOTE_CLIENT_MENUGEN_H
#define REMOTE_CLIENT_MENUGEN_H

#include <stdint.h>

#include "menu.h"

/* The most entries of a random menu.  */
enum { MAX_ENTRIES = 40 };

/* The most entries of a menu in a test: a random menu in a nest of
   RC_MENU_MAX_DEPTH + 1 submenus.  */
enum { MAX_MENU = MAX_ENTRIES + 2 * (RC_MENU_MAX_DEPTH + 1) };

/* The deepest nest of submenus of a random menu.  */
enum { MAX_RANDOM_DEPTH = 4 };

/* A sequence of entries.  */
struct menu
{
  struct rd_menu_item entries[MAX_MENU];
  int n;
};

/* Make in MENU a random menu SERIAL from the generator STATE.  */
extern void random_menu (uint64_t *state, uint32_t serial, struct menu *menu);

/* Make in ENTRY a random entry of the kind KIND of the menu SERIAL.
   Only an item has an id.  */
extern void random_entry (uint64_t *state, uint32_t serial, uint8_t kind,
			  struct rd_menu_item *entry);

/* Put N random bytes in TEXT.  Some texts are UTF-8, and the end of
   such a text can cut a character.  The other texts have any
   bytes.  */
extern void random_text (uint64_t *state, uint8_t *text, int n);

/* Return a submenu-end entry of the menu SERIAL.  */
extern struct rd_menu_item end_entry (uint32_t serial);

/* Return a random number from 0 to LIMIT - 1.  */
extern uint64_t random_below (uint64_t *state, uint64_t limit);

/* Return the next random number of STATE.  */
extern uint64_t random_next (uint64_t *state);

#endif /* REMOTE_CLIENT_MENUGEN_H */
