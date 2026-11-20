/* The tests of the end of a session for the remote display client.

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

/* The laws of the end of a session, for each reason from 0 to 255:

   - Status: the exit status of the program after an end-session
     message is 0 for the reason RD_END_CLOSED, and 1 for each other
     reason.  A reason that the program does not know is an error.

   - Encode: rd_encode_end_session with a buffer of at least
     rd_encoded_size bytes writes the end-session message of the
     reason, and returns that size.  With a smaller buffer, it writes
     nothing, and returns 0.

   The test checks all the reasons, so it needs no seed.  */

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ending.h"
#include "rdproto.h"

static bool status_law (void);
static bool encode_law (void);
static bool encodes_reason (uint8_t);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} laws[] =
  {
    { "status", status_law },
    { "encode", encode_law },
  };

int
main (void)
{
  for (size_t i = 0; i < sizeof laws / sizeof *laws; i++)
    if (!laws[i].check ())
      {
	fprintf (stderr, "test-ending: the %s law failed\n", laws[i].name);
	return 1;
      }
  printf ("test-ending: the laws passed for each reason\n");
  return 0;
}

/* The status law.  */
static bool
status_law (void)
{
  for (int reason = 0; reason <= UINT8_MAX; reason++)
    if (rc_end_status (reason) != (reason == RD_END_CLOSED ? 0 : 1))
      {
	fprintf (stderr, "test-ending: reason %d gives status %d\n", reason,
		 rc_end_status (reason));
	return false;
      }
  return true;
}

/* The encode law.  */
static bool
encode_law (void)
{
  for (int reason = 0; reason <= UINT8_MAX; reason++)
    if (!encodes_reason (reason))
      {
	fprintf (stderr, "test-ending: reason %d does not encode\n", reason);
	return false;
      }
  return true;
}

/* Return true if the encode law holds for REASON, for each size of the
   buffer up to two more bytes than the message.  */
static bool
encodes_reason (uint8_t reason)
{
  struct rd_message expected = { .type = RD_END_SESSION,
				 .u.end_session = { .reason = reason } };
  size_t needed = rd_encoded_size (&expected);
  unsigned char bytes[64];
  for (size_t size = 0; size <= needed + 2; size++)
    {
      memset (bytes, 0xaa, sizeof bytes);
      size_t written = rd_encode_end_session (reason, bytes, size);
      struct rd_message got;
      size_t used;
      bool ok = (size < needed
		 ? written == 0 && bytes[0] == 0xaa
		 : (written == needed
		    && rd_decode (bytes, written, &got, &used) == RD_OK
		    && used == needed && got.type == RD_END_SESSION
		    && got.u.end_session.reason == reason));
      if (!ok)
	return false;
    }
  return true;
}
