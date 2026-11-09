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

/* A table gives a value that is not NULL to each of its IDs.  A server
   has few frames and few fonts, so a search of a table is linear.
   The struct is public only so that a table can be a member of another
   struct.  Initialize a table with all fields 0, and use it only
   through these functions.  */

#ifndef REMOTE_CLIENT_TABLE_H
#define REMOTE_CLIENT_TABLE_H

#include <stdint.h>

struct rc_entry
{
  uint32_t id;
  void *value;
};

struct rc_table
{
  struct rc_entry *entries;
  int count;
  int size;
};

/* Return the value of ID in TABLE, or NULL if TABLE does not have
   ID.  */
extern void *rc_table_get (const struct rc_table *table, uint32_t id);

/* Give ID the value VALUE in TABLE.  VALUE must not be NULL.  */
extern void rc_table_set (struct rc_table *table, uint32_t id,
			  void *value);

/* Remove ID from TABLE, and return its value.  Return NULL if TABLE
   does not have ID.  */
extern void *rc_table_remove (struct rc_table *table, uint32_t id);

/* Call DESTROY for each value of TABLE, and free the memory of
   TABLE.  */
extern void rc_table_free (struct rc_table *table,
			   void (*destroy) (void *value));

#endif /* REMOTE_CLIENT_TABLE_H */
