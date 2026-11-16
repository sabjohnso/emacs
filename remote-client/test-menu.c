/* Test the menu model of the client.

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

/* The laws of menu.c, for random menus.  A random menu is a sequence
   of entries that nest correctly, with at least one entry.  Its
   items, separators and submenus have random flags, labels and keys.
   Its submenu-end entries have only the kind.

   - Round trip: the entries of a menu, and then the show of its
     serial, give a tree.  The preorder walk of the tree gives the
     entries again: a submenu-start entry, the walk of its children,
     and a submenu-end entry.  The show takes the entries, so a second
     show of the serial gives no tree.
   - New serial: the first entries of a menu, and then all entries of
     a menu with another serial, give the tree of the second menu.  A
     show of the first serial then gives no tree.
   - Damage: a show with another serial than the entries gives no
     tree.  Entries with a submenu-end entry and no submenu-start
     entry before it, with a submenu-start entry and no submenu-end
     entry after it, with an entry of an unknown kind, or with a nest
     of submenus deeper than RC_MENU_MAX_DEPTH give no tree.  A nest
     of exactly RC_MENU_MAX_DEPTH submenus gives a tree.
   - Ids: the ids of the item nodes of a tree, in preorder, are the
     ids of the item entries of the menu, in order.
   - Hide: a hide of the serial of the entries drops them, and a show
     then gives no tree.  A hide of another serial keeps them, and a
     show then gives their tree.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "menu.h"

enum { TRIALS = 300 };

/* The most entries of a random menu, before the nest of the depth
   law.  */
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

static const char *run_trial (uint64_t);
static bool round_trip_law (uint64_t *);
static bool new_serial_law (uint64_t *);
static bool damage_law (uint64_t *);
static bool ids_law (uint64_t *);
static bool hide_law (uint64_t *);
static void random_menu (uint64_t *, uint32_t, struct menu *);
static void random_entries (uint64_t *, uint32_t, int, struct menu *);
static void random_entry (uint64_t *, uint32_t, uint8_t,
			  struct rd_menu_item *);
static struct rd_menu_item end_entry (uint32_t);
static void flatten (struct menu *);
static void nest (struct menu *, int);
static void insert (struct menu *, int, struct rd_menu_item);
static void delete (struct menu *, int);
static int random_index_of_kind (uint64_t *, const struct menu *, uint8_t);
static void add_all (struct rc_menu_model *, const struct menu *);
static bool shows (const struct menu *, uint32_t);
static bool same_tree (const struct rc_menu *, const struct menu *);
static void walk (const struct rc_menu_node *, size_t, uint32_t,
		  struct menu *);
static bool same_entry (const struct rd_menu_item *,
			const struct rd_menu_item *);
static void item_ids (const struct rc_menu_node *, size_t, uint32_t *,
		      int *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "round trip", round_trip_law },
    { "new serial", new_serial_law },
    { "damage", damage_law },
    { "ids", ids_law },
    { "hide", hide_law },
  };

int
main (int argc, char **argv)
{
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 1 : 0;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    {
      const char *failed = run_trial (base + trial);
      if (failed)
	{
	  fprintf (stderr, "test-menu: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-menu: %d trials passed\n", TRIALS);
  return 0;
}

/* Run each law with SEED.  Return the name of the first law that
   fails, or NULL.  */
static const char *
run_trial (uint64_t seed)
{
  for (size_t i = 0; i < sizeof laws / sizeof *laws; i++)
    {
      uint64_t state = seed;
      if (!laws[i].check (&state))
	return laws[i].name;
    }
  return NULL;
}

static bool
round_trip_law (uint64_t *state)
{
  static struct menu menu;
  uint32_t serial = random_next (state);
  random_menu (state, serial, &menu);
  struct rc_menu_model model = { 0 };
  add_all (&model, &menu);
  struct rc_menu *tree = rc_menu_show (&model, serial);
  struct rc_menu *again = rc_menu_show (&model, serial);
  bool passed = tree && same_tree (tree, &menu) && !again;
  rc_menu_free (tree);
  rc_menu_free (again);
  rc_menu_model_free (&model);
  return passed;
}

static bool
new_serial_law (uint64_t *state)
{
  static struct menu first, second;
  uint32_t serial = random_next (state);
  uint32_t other = serial + 1 + random_below (state, 1000);
  random_menu (state, serial, &first);
  random_menu (state, other, &second);
  struct rc_menu_model model = { 0 };
  int sent = random_below (state, first.n + 1);
  for (int i = 0; i < sent; i++)
    rc_menu_add (&model, &first.entries[i]);
  add_all (&model, &second);
  struct rc_menu *old = rc_menu_show (&model, serial);
  struct rc_menu *tree = rc_menu_show (&model, other);
  bool passed = !old && tree && same_tree (tree, &second);
  rc_menu_free (old);
  rc_menu_free (tree);
  rc_menu_model_free (&model);
  return passed;
}

/* The kinds of damage of the damage law.  */
enum damage
  {
    OTHER_SERIAL, NO_START, NO_END, UNKNOWN_KIND, TOO_DEEP, DAMAGE_KINDS
  };

static bool
damage_law (uint64_t *state)
{
  static struct menu menu;
  uint32_t serial = random_next (state);
  random_menu (state, serial, &menu);
  enum damage damage = random_below (state, DAMAGE_KINDS);
  uint32_t shown = serial;
  if (damage == OTHER_SERIAL)
    shown = serial + 1 + random_below (state, 1000);
  else if (damage == NO_START)
    {
      /* An end with no start: drop a start, or add an end before the
	 first entry or after the last entry.  */
      int start = random_index_of_kind (state, &menu,
					RD_MENU_KIND_SUBMENU_START);
      if (start >= 0 && random_below (state, 2))
	delete (&menu, start);
      else
	insert (&menu, random_below (state, 2) ? menu.n : 0,
		end_entry (serial));
    }
  else if (damage == NO_END)
    {
      /* A start with no end: drop an end, or add a start after the
	 last entry.  */
      int end = random_index_of_kind (state, &menu,
				      RD_MENU_KIND_SUBMENU_END);
      if (end >= 0 && random_below (state, 2))
	delete (&menu, end);
      else
	{
	  struct rd_menu_item start;
	  random_entry (state, serial, RD_MENU_KIND_SUBMENU_START, &start);
	  insert (&menu, menu.n, start);
	}
    }
  else if (damage == UNKNOWN_KIND)
    menu.entries[random_below (state, menu.n)].kind
      = RD_MENU_KIND_SUBMENU_END + 1 + random_below (state, 252);
  else
    nest (&menu, RC_MENU_MAX_DEPTH + 1);
  bool passed = !shows (&menu, shown);
  if (damage == TOO_DEEP)
    {
      /* A random menu with no submenus, in a nest of exactly
	 RC_MENU_MAX_DEPTH submenus.  */
      random_menu (state, serial, &menu);
      flatten (&menu);
      nest (&menu, RC_MENU_MAX_DEPTH);
      passed = passed && shows (&menu, serial);
    }
  return passed;
}

static bool
ids_law (uint64_t *state)
{
  static struct menu menu;
  uint32_t serial = random_next (state);
  random_menu (state, serial, &menu);
  uint32_t expected[MAX_MENU], ids[MAX_MENU];
  int nexpected = 0, nids = 0;
  for (int i = 0; i < menu.n; i++)
    if (menu.entries[i].kind == RD_MENU_KIND_ITEM)
      expected[nexpected++] = menu.entries[i].id;
  struct rc_menu_model model = { 0 };
  add_all (&model, &menu);
  struct rc_menu *tree = rc_menu_show (&model, serial);
  if (tree)
    item_ids (tree->children, tree->nchildren, ids, &nids);
  bool passed = (tree && nids == nexpected
		 && !memcmp (ids, expected, nids * sizeof *ids));
  rc_menu_free (tree);
  rc_menu_model_free (&model);
  return passed;
}

static bool
hide_law (uint64_t *state)
{
  static struct menu menu;
  uint32_t serial = random_next (state);
  random_menu (state, serial, &menu);
  bool same = random_below (state, 2);
  uint32_t hidden = same ? serial : serial + 1 + random_below (state, 1000);
  struct rc_menu_model model = { 0 };
  add_all (&model, &menu);
  rc_menu_hide (&model, hidden);
  struct rc_menu *tree = rc_menu_show (&model, serial);
  bool passed = same ? !tree : tree && same_tree (tree, &menu);
  rc_menu_free (tree);
  rc_menu_model_free (&model);
  return passed;
}

/* Make in MENU a random menu SERIAL with at least one entry.  */
static void
random_menu (uint64_t *state, uint32_t serial, struct menu *menu)
{
  do
    {
      menu->n = 0;
      random_entries (state, serial, MAX_RANDOM_DEPTH, menu);
    }
  while (menu->n == 0);
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

/* Make in ENTRY a random entry of the kind KIND of the menu SERIAL.
   Only an item has an id.  Some labels have the largest size.  */
static void
random_entry (uint64_t *state, uint32_t serial, uint8_t kind,
	      struct rd_menu_item *entry)
{
  entry->serial = serial;
  entry->kind = kind;
  entry->flags = random_below (state, 16);
  entry->id = kind == RD_MENU_KIND_ITEM ? random_next (state) : 0;
  entry->nlabel = (random_below (state, 10) == 0 ? RD_MAX_MENU_TEXT
		   : random_below (state, 40));
  for (int i = 0; i < entry->nlabel; i++)
    entry->label[i] = random_next (state);
  entry->nkey = random_below (state, RD_MAX_MENU_KEY + 1);
  for (int i = 0; i < entry->nkey; i++)
    entry->key[i] = random_next (state);
}

/* Return a submenu-end entry of the menu SERIAL.  */
static struct rd_menu_item
end_entry (uint32_t serial)
{
  return (struct rd_menu_item) { .serial = serial,
				 .kind = RD_MENU_KIND_SUBMENU_END };
}

/* Delete the submenu-start and the submenu-end entries of MENU.  Keep
   at least one entry.  */
static void
flatten (struct menu *menu)
{
  int n = 0;
  for (int i = 0; i < menu->n; i++)
    if (menu->entries[i].kind != RD_MENU_KIND_SUBMENU_START
	&& menu->entries[i].kind != RD_MENU_KIND_SUBMENU_END)
      menu->entries[n++] = menu->entries[i];
  if (n == 0)
    menu->entries[n++] = (struct rd_menu_item)
      { .serial = menu->entries[0].serial, .kind = RD_MENU_KIND_ITEM };
  menu->n = n;
}

/* Put the entries of MENU in a nest of DEPTH submenus.  */
static void
nest (struct menu *menu, int depth)
{
  uint32_t serial = menu->entries[0].serial;
  for (int i = 0; i < depth; i++)
    {
      insert (menu, 0, (struct rd_menu_item)
	      { .serial = serial, .kind = RD_MENU_KIND_SUBMENU_START });
      insert (menu, menu->n, end_entry (serial));
    }
}

/* Insert ENTRY in MENU before the entry INDEX.  */
static void
insert (struct menu *menu, int index, struct rd_menu_item entry)
{
  memmove (&menu->entries[index + 1], &menu->entries[index],
	   (menu->n - index) * sizeof *menu->entries);
  menu->entries[index] = entry;
  menu->n++;
}

/* Delete the entry INDEX of MENU.  */
static void
delete (struct menu *menu, int index)
{
  memmove (&menu->entries[index], &menu->entries[index + 1],
	   (menu->n - index - 1) * sizeof *menu->entries);
  menu->n--;
}

/* Return the index of a random entry of the kind KIND of MENU, or -1
   if MENU has no entry of that kind.  */
static int
random_index_of_kind (uint64_t *state, const struct menu *menu,
		      uint8_t kind)
{
  int indexes[MAX_MENU], n = 0;
  for (int i = 0; i < menu->n; i++)
    if (menu->entries[i].kind == kind)
      indexes[n++] = i;
  return n ? indexes[random_below (state, n)] : -1;
}

/* Add each entry of MENU to MODEL, in order.  */
static void
add_all (struct rc_menu_model *model, const struct menu *menu)
{
  for (int i = 0; i < menu->n; i++)
    rc_menu_add (model, &menu->entries[i]);
}

/* Return true if the entries of MENU, and then a show of SERIAL, give
   a tree.  */
static bool
shows (const struct menu *menu, uint32_t serial)
{
  struct rc_menu_model model = { 0 };
  add_all (&model, menu);
  struct rc_menu *tree = rc_menu_show (&model, serial);
  bool shown = tree != NULL;
  rc_menu_free (tree);
  rc_menu_model_free (&model);
  return shown;
}

/* Return true if TREE has the serial of MENU, and its preorder walk
   gives the entries of MENU.  */
static bool
same_tree (const struct rc_menu *tree, const struct menu *menu)
{
  static struct menu walked;
  uint32_t serial = menu->entries[0].serial;
  walked.n = 0;
  walk (tree->children, tree->nchildren, serial, &walked);
  bool passed = tree->serial == serial && walked.n == menu->n;
  for (int i = 0; passed && i < menu->n; i++)
    passed = same_entry (&walked.entries[i], &menu->entries[i]);
  return passed;
}

/* Add to WALKED the entries of the menu SERIAL that the N nodes NODES
   give in preorder.  */
static void
walk (const struct rc_menu_node *nodes, size_t n, uint32_t serial,
      struct menu *walked)
{
  for (size_t i = 0; i < n && walked->n < MAX_MENU; i++)
    {
      const struct rc_menu_node *node = &nodes[i];
      struct rd_menu_item *entry = &walked->entries[walked->n++];
      *entry = (struct rd_menu_item) { .serial = serial, .kind = node->kind,
				       .flags = node->flags, .id = node->id };
      entry->nlabel = node->nlabel < RD_MAX_MENU_TEXT
		      ? node->nlabel : RD_MAX_MENU_TEXT;
      memcpy (entry->label, node->label, entry->nlabel);
      entry->nkey = node->nkey < RD_MAX_MENU_KEY ? node->nkey
		    : RD_MAX_MENU_KEY;
      memcpy (entry->key, node->key, entry->nkey);
      if (node->label[node->nlabel] || node->key[node->nkey])
	/* The texts must end with a NUL byte.  Make the walk differ
	   from the menu.  */
	entry->kind = RD_MENU_KIND_SUBMENU_END + 1;
      if (node->kind == RD_MENU_KIND_SUBMENU_START)
	{
	  walk (node->children, node->nchildren, serial, walked);
	  if (walked->n < MAX_MENU)
	    walked->entries[walked->n++] = end_entry (serial);
	}
      else if (node->nchildren > 0)
	entry->kind = RD_MENU_KIND_SUBMENU_END + 1;
    }
}

/* Return true if the entries A and B have the same fields.  */
static bool
same_entry (const struct rd_menu_item *a, const struct rd_menu_item *b)
{
  return (a->serial == b->serial && a->kind == b->kind
	  && a->flags == b->flags && a->id == b->id
	  && a->nlabel == b->nlabel
	  && !memcmp (a->label, b->label, a->nlabel)
	  && a->nkey == b->nkey && !memcmp (a->key, b->key, a->nkey));
}

/* Add to IDS the ids of the item nodes of the N nodes NODES, in
   preorder.  *NIDS is the number of ids in IDS.  */
static void
item_ids (const struct rc_menu_node *nodes, size_t n, uint32_t *ids,
	  int *nids)
{
  for (size_t i = 0; i < n && *nids < MAX_MENU; i++)
    {
      if (nodes[i].kind == RD_MENU_KIND_ITEM)
	ids[(*nids)++] = nodes[i].id;
      item_ids (nodes[i].children, nodes[i].nchildren, ids, nids);
    }
}

static uint64_t
random_below (uint64_t *state, uint64_t limit)
{
  return random_next (state) % limit;
}

/* Return the next number of the generator splitmix64.  */
static uint64_t
random_next (uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}
