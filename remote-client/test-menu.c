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

/* The laws of menu.c, for random menus (menugen.h).

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
#include "menugen.h"

enum { TRIALS = 300 };

static const char *run_trial (uint64_t);
static bool round_trip_law (uint64_t *);
static bool new_serial_law (uint64_t *);
static bool damage_law (uint64_t *);
static bool ids_law (uint64_t *);
static bool hide_law (uint64_t *);
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
