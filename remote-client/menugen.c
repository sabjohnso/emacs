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


#include <config.h>

#include <stdbool.h>

#include "menugen.h"

static void random_entries (uint64_t *, uint32_t, int, struct menu *);

void
random_menu (uint64_t *state, uint32_t serial, struct menu *menu)
{
  do
    {
      menu->n = 0;
      random_entries (state, serial, MAX_RANDOM_DEPTH, menu);
    }
  while (menu->n == 0);
}

/* Some labels have the largest size.  */
void
random_entry (uint64_t *state, uint32_t serial, uint8_t kind,
	      struct rd_menu_item *entry)
{
  entry->serial = serial;
  entry->kind = kind;
  entry->flags = random_below (state, 16);
  entry->id = kind == RD_MENU_KIND_ITEM ? random_next (state) : 0;
  entry->nlabel = (random_below (state, 10) == 0 ? RD_MAX_MENU_TEXT
		   : random_below (state, 40));
  random_text (state, entry->label, entry->nlabel);
  entry->nkey = random_below (state, RD_MAX_MENU_KEY + 1);
  random_text (state, entry->key, entry->nkey);
}

/* The UTF-8 text has characters of 1, 2, 3 and 4 bytes.  */
void
random_text (uint64_t *state, uint8_t *text, int n)
{
  static const char *const characters[] =
    { "a", "\303\251", "\342\202\254", "\360\237\230\200" };
  bool utf8 = random_below (state, 2);
  for (int i = 0; i < n;)
    if (utf8)
      {
	const char *c = characters[random_below (state, 4)];
	for (int j = 0; c[j] && i < n; j++)
	  text[i++] = c[j];
      }
    else
      text[i++] = random_next (state);
}

struct rd_menu_item
end_entry (uint32_t serial)
{
  return (struct rd_menu_item) { .serial = serial,
				 .kind = RD_MENU_KIND_SUBMENU_END };
}

uint64_t
random_below (uint64_t *state, uint64_t limit)
{
  return random_next (state) % limit;
}

/* This is the generator splitmix64.  */
uint64_t
random_next (uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}

/* Add to MENU random entries of the menu SERIAL that nest correctly,
   with a nest of at most DEPTH submenus.  */
static void
random_entries (uint64_t *state, uint32_t serial, int depth,
		struct menu *menu)
{
  int count = random_below (state, 8);
  /* Keep room for the submenu-end entries of the open submenus.  */
  for (int i = 0;
       i < count && menu->n + 2 * (MAX_RANDOM_DEPTH + 1) <= MAX_ENTRIES;
       i++)
    {
      uint8_t kind = random_below (state, 3);
      if (kind == RD_MENU_KIND_SUBMENU_START && depth == 0)
	kind = RD_MENU_KIND_ITEM;
      random_entry (state, serial, kind, &menu->entries[menu->n++]);
      if (kind == RD_MENU_KIND_SUBMENU_START)
	{
	  random_entries (state, serial, depth - 1, menu);
	  menu->entries[menu->n++] = end_entry (serial);
	}
    }
}
