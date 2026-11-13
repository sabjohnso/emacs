/* The schedule of the reconnects of remote-client.

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

#include "backoff.h"

struct rc_backoff
rc_backoff_new (void)
{
  return (struct rc_backoff) { .next = RC_BACKOFF_FIRST };
}

int64_t
rc_backoff_next (struct rc_backoff *backoff)
{
  int64_t wait = backoff->next;
  backoff->next = (wait < RC_BACKOFF_LIMIT / 2 ? 2 * wait
		   : RC_BACKOFF_LIMIT);
  return wait;
}

void
rc_backoff_reset (struct rc_backoff *backoff)
{
  *backoff = rc_backoff_new ();
}
