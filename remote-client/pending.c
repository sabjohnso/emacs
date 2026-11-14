/* The connections of the relay that wait for their token.

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

#include <assert.h>

#include "pending.h"

static int earliest (const struct rc_pending_table *);
static struct rc_pending *free_slot (struct rc_pending_table *);

void
rc_pending_init (struct rc_pending_table *table)
{
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    table->slots[i].fd = -1;
}

int
rc_pending_add (struct rc_pending_table *table, int fd, int64_t deadline)
{
  assert (fd >= 0);
  struct rc_pending *slot = free_slot (table);
  int removed = -1;
  if (!slot)
    {
      slot = &table->slots[earliest (table)];
      removed = slot->fd;
    }
  *slot = (struct rc_pending) { .fd = fd, .got = 0, .deadline = deadline };
  return removed;
}

struct rc_pending *
rc_pending_find (struct rc_pending_table *table, int fd)
{
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    if (table->slots[i].fd == fd)
      return &table->slots[i];
  return NULL;
}

void
rc_pending_remove (struct rc_pending *slot)
{
  slot->fd = -1;
}

uint8_t *
rc_pending_space (struct rc_pending *slot, size_t *size)
{
  *size = RC_TOKEN_SIZE - slot->got;
  return slot->token + slot->got;
}

enum rc_pending_result
rc_pending_fill (struct rc_pending *slot, size_t count,
		 const uint8_t expected[RC_TOKEN_SIZE])
{
  assert (count <= RC_TOKEN_SIZE - slot->got);
  slot->got += count;
  if (slot->got < RC_TOKEN_SIZE)
    return RC_PENDING_MORE;
  return (rc_token_equal (slot->token, expected)
	  ? RC_PENDING_GOOD : RC_PENDING_BAD);
}

int
rc_pending_fd (const struct rc_pending_table *table, int i)
{
  return table->slots[i].fd;
}

int
rc_pending_count (const struct rc_pending_table *table)
{
  int count = 0;
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    count += table->slots[i].fd >= 0;
  return count;
}

int64_t
rc_pending_wait (const struct rc_pending_table *table, int64_t now)
{
  int first = earliest (table);
  if (first < 0)
    return -1;
  int64_t deadline = table->slots[first].deadline;
  return deadline > now ? deadline - now : 0;
}

int
rc_pending_expired (struct rc_pending_table *table, int64_t now)
{
  int first = earliest (table);
  if (first < 0 || table->slots[first].deadline > now)
    return -1;
  int fd = table->slots[first].fd;
  rc_pending_remove (&table->slots[first]);
  return fd;
}

/* Return the index of the slot of TABLE with the earliest deadline,
   or -1 if TABLE is empty.  */
static int
earliest (const struct rc_pending_table *table)
{
  int first = -1;
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    if (table->slots[i].fd >= 0
	&& (first < 0
	    || table->slots[i].deadline < table->slots[first].deadline))
      first = i;
  return first;
}

/* Return a free slot of TABLE, or NULL if TABLE is full.  */
static struct rc_pending *
free_slot (struct rc_pending_table *table)
{
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    if (table->slots[i].fd < 0)
      return &table->slots[i];
  return NULL;
}
