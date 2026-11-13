/* Tables of IDs for the remote display client.

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

/* The client cannot continue with no memory, so a failed allocation
   stops the program.  */

#include <config.h>

#include <stdlib.h>

#include "table.h"

static int entry_index (const struct rc_table *, uint32_t);

void *
rc_table_get (const struct rc_table *table, uint32_t id)
{
  int i = entry_index (table, id);
  return i < 0 ? NULL : table->entries[i].value;
}

void
rc_table_set (struct rc_table *table, uint32_t id, void *value)
{
  int i = entry_index (table, id);
  if (i >= 0)
    {
      table->entries[i].value = value;
      return;
    }
  if (table->count == table->size)
    {
      table->size = table->size ? 2 * table->size : 4;
      table->entries = realloc (table->entries,
				table->size * sizeof *table->entries);
      if (!table->entries)
	abort ();
    }
  table->entries[table->count++] = (struct rc_entry) { id, value };
}

void *
rc_table_remove (struct rc_table *table, uint32_t id)
{
  int i = entry_index (table, id);
  if (i < 0)
    return NULL;
  void *value = table->entries[i].value;
  table->entries[i] = table->entries[--table->count];
  return value;
}

int
rc_table_count (const struct rc_table *table)
{
  return table->count;
}

void
rc_table_each (const struct rc_table *table,
	       void (*function) (uint32_t, void *, void *), void *data)
{
  for (int i = 0; i < table->count; i++)
    function (table->entries[i].id, table->entries[i].value, data);
}

void
rc_table_free (struct rc_table *table, void (*destroy) (void *))
{
  for (int i = 0; i < table->count; i++)
    destroy (table->entries[i].value);
  free (table->entries);
  *table = (struct rc_table) { 0 };
}

/* Return the position of ID in the entries of TABLE, or -1 if TABLE
   does not have ID.  */
static int
entry_index (const struct rc_table *table, uint32_t id)
{
  for (int i = 0; i < table->count; i++)
    if (table->entries[i].id == id)
      return i;
  return -1;
}
