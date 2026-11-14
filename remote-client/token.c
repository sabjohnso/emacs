/* The token of the relay of remote-client.

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

#include <sys/random.h>

#include "token.h"

bool
rc_token_new (uint8_t token[RC_TOKEN_SIZE])
{
  return getrandom (token, RC_TOKEN_SIZE, 0) == RC_TOKEN_SIZE;
}

bool
rc_token_equal (const uint8_t a[RC_TOKEN_SIZE],
		const uint8_t b[RC_TOKEN_SIZE])
{
  uint8_t difference = 0;
  for (int i = 0; i < RC_TOKEN_SIZE; i++)
    difference |= a[i] ^ b[i];
  return difference == 0;
}
