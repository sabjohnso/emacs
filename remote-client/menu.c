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

/* The model keeps the entries of a menu as a flat array of nodes with
   no children.  A show checks that the entries nest correctly, and
   then moves each node into the children of its submenu.  */

#include <config.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"

static bool nests_correctly (const struct rc_menu_node *, size_t);
static size_t build (struct rc_menu_node *, size_t, struct rc_menu_node **,
		     size_t *);
static size_t count_children (const struct rc_menu_node *, size_t);
static struct rc_menu_node node_of (const struct rd_menu_item *);
static char *copy_text (const uint8_t *, size_t);
static void free_nodes (struct rc_menu_node *, size_t);
static void *xmalloc (size_t);

void
rc_menu_add (struct rc_menu_model *model, const struct rd_menu_item *item)
{
  if (model->serial != item->serial)
    rc_menu_hide (model, model->serial);
  if (model->nentries == model->size)
    {
      model->size = model->size ? 2 * model->size : 16;
      model->entries = realloc (model->entries,
				model->size * sizeof *model->entries);
      if (!model->entries)
	abort ();
    }
  model->serial = item->serial;
  model->entries[model->nentries++] = node_of (item);
}

struct rc_menu *
rc_menu_show (struct rc_menu_model *model, uint32_t serial)
{
  if (model->nentries == 0 || model->serial != serial)
    return NULL;
  struct rc_menu *menu = NULL;
  if (nests_correctly (model->entries, model->nentries))
    {
      menu = xmalloc (sizeof *menu);
      menu->serial = serial;
      build (model->entries, model->nentries, &menu->children,
	     &menu->nchildren);
      model->nentries = 0;
    }
  rc_menu_hide (model, serial);
  return menu;
}

void
rc_menu_hide (struct rc_menu_model *model, uint32_t serial)
{
  if (model->serial != serial)
    return;
  free_nodes (model->entries, model->nentries);
  model->nentries = 0;
}

void
rc_menu_model_free (struct rc_menu_model *model)
{
  rc_menu_hide (model, model->serial);
  free (model->entries);
  *model = (struct rc_menu_model) { 0 };
}

void
rc_menu_free (struct rc_menu *menu)
{
  if (!menu)
    return;
  free_nodes (menu->children, menu->nchildren);
  free (menu->children);
  free (menu);
}

/* Return true if each kind of the N nodes NODES is known, each
   submenu-start node has a submenu-end node after it, each
   submenu-end node has a submenu-start node before it, and the nest
   is at most RC_MENU_MAX_DEPTH deep.  */
static bool
nests_correctly (const struct rc_menu_node *nodes, size_t n)
{
  int depth = 0;
  for (size_t i = 0; i < n; i++)
    {
      if (nodes[i].kind == RD_MENU_KIND_SUBMENU_START)
	depth++;
      else if (nodes[i].kind == RD_MENU_KIND_SUBMENU_END)
	depth--;
      else if (nodes[i].kind != RD_MENU_KIND_ITEM
	       && nodes[i].kind != RD_MENU_KIND_SEPARATOR)
	return false;
      if (depth < 0 || depth > RC_MENU_MAX_DEPTH)
	return false;
    }
  return depth == 0;
}

/* Move the first nodes of the N nodes NODES into the new array
   *CHILDREN of *NCHILDREN nodes, and the nodes of each submenu into
   the children of its node.  Stop at the first submenu-end node that
   closes no submenu of NODES, and free it.  The nodes must nest
   correctly.  Return the number of nodes that the move used, with
   that submenu-end node.  */
static size_t
build (struct rc_menu_node *nodes, size_t n, struct rc_menu_node **children,
       size_t *nchildren)
{
  *nchildren = count_children (nodes, n);
  *children = xmalloc ((*nchildren ? *nchildren : 1) * sizeof **children);
  size_t i = 0;
  for (size_t child = 0; child < *nchildren; child++)
    {
      struct rc_menu_node *node = &(*children)[child];
      *node = nodes[i++];
      if (node->kind == RD_MENU_KIND_SUBMENU_START)
	i += build (&nodes[i], n - i, &node->children, &node->nchildren);
    }
  if (i < n)
    free_nodes (&nodes[i++], 1);
  return i;
}

/* Return the number of nodes of the N nodes NODES that are not in a
   submenu, before the first submenu-end node that closes no submenu
   of NODES.  */
static size_t
count_children (const struct rc_menu_node *nodes, size_t n)
{
  size_t count = 0;
  int depth = 0;
  for (size_t i = 0; i < n && depth >= 0; i++)
    {
      if (nodes[i].kind == RD_MENU_KIND_SUBMENU_END)
	depth--;
      else
	{
	  count += depth == 0;
	  depth += nodes[i].kind == RD_MENU_KIND_SUBMENU_START;
	}
    }
  return count;
}

/* Return a node with the fields of ITEM and no children.  */
static struct rc_menu_node
node_of (const struct rd_menu_item *item)
{
  return (struct rc_menu_node)
    {
      .kind = item->kind,
      .flags = item->flags,
      .id = item->id,
      .label = copy_text (item->label, item->nlabel),
      .nlabel = item->nlabel,
      .key = copy_text (item->key, item->nkey),
      .nkey = item->nkey,
    };
}

/* Return a copy of the N bytes BYTES with a NUL byte after them.  */
static char *
copy_text (const uint8_t *bytes, size_t n)
{
  char *text = xmalloc (n + 1);
  memcpy (text, bytes, n);
  text[n] = '\0';
  return text;
}

/* Free the texts and the children of the N nodes NODES, but not the
   array NODES.  */
static void
free_nodes (struct rc_menu_node *nodes, size_t n)
{
  for (size_t i = 0; i < n; i++)
    {
      free (nodes[i].label);
      free (nodes[i].key);
      free_nodes (nodes[i].children, nodes[i].nchildren);
      free (nodes[i].children);
    }
}

/* Return SIZE new bytes.  Stop the program if there is no memory.  */
static void *
xmalloc (size_t size)
{
  void *p = malloc (size);
  if (!p)
    abort ();
  return p;
}
