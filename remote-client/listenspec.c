/* The listen spec of the relay of remote-client.

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

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "listenspec.h"

static bool parse_address (const char *, size_t, struct rc_listen_spec *);
static bool parse_ports (const char *, struct rc_listen_spec *);
static const char *parse_port (const char *, uint16_t *);

bool
rc_listen_spec_parse (const char *text, struct rc_listen_spec *spec)
{
  struct rc_listen_spec result = { .low = RC_LISTEN_LOW,
				   .high = RC_LISTEN_HIGH };
  const char *ports = NULL;
  bool ok;
  if (text[0] == '[')
    {
      const char *close = strchr (text, ']');
      ok = close && parse_address (text + 1, close - text - 1, &result)
	&& result.family == AF_INET6
	&& (close[1] == 0 || close[1] == ':');
      ports = ok && close[1] == ':' ? close + 2 : NULL;
    }
  else if (strchr (text, ':') != strrchr (text, ':'))
    ok = parse_address (text, strlen (text), &result);
  else
    {
      const char *colon = strchr (text, ':');
      size_t length = colon ? (size_t) (colon - text) : strlen (text);
      ok = parse_address (text, length, &result)
	&& result.family == AF_INET;
      ports = colon ? colon + 1 : NULL;
    }
  if (!ok || (ports && !parse_ports (ports, &result)))
    return false;
  *spec = result;
  return true;
}

void
rc_listen_spec_format (const struct rc_listen_spec *spec,
		       char text[RC_LISTEN_TEXT_SIZE])
{
  char address[INET6_ADDRSTRLEN];
  inet_ntop (spec->family, &spec->address, address, sizeof address);
  snprintf (text, RC_LISTEN_TEXT_SIZE,
	    spec->family == AF_INET6 ? "[%s]:%u-%u" : "%s:%u-%u", address,
	    (unsigned) spec->low, (unsigned) spec->high);
}

socklen_t
rc_listen_spec_address (const struct rc_listen_spec *spec, uint16_t port,
			struct sockaddr_storage *address)
{
  memset (address, 0, sizeof *address);
  if (spec->family == AF_INET6)
    {
      struct sockaddr_in6 *v6 = (struct sockaddr_in6 *) address;
      v6->sin6_family = AF_INET6;
      v6->sin6_port = htons (port);
      v6->sin6_addr = spec->address.v6;
      return sizeof *v6;
    }
  struct sockaddr_in *v4 = (struct sockaddr_in *) address;
  v4->sin_family = AF_INET;
  v4->sin_port = htons (port);
  v4->sin_addr = spec->address.v4;
  return sizeof *v4;
}

bool
rc_listen_spec_from_connection (const char *connection,
				struct rc_listen_spec *spec)
{
  /* The fields are separated by one space, and none is empty.  */
  const char *starts[4];
  int fields = 0;
  for (const char *at = connection; fields < 4; at++)
    {
      if (*at == 0 || *at == ' ')
	return false;
      starts[fields++] = at;
      at = strchr (at, ' ');
      if (!at)
	break;
    }
  if (fields != 4 || strchr (starts[3], ' '))
    return false;
  struct rc_listen_spec result = { .low = RC_LISTEN_LOW,
				   .high = RC_LISTEN_HIGH };
  if (!parse_address (starts[2], starts[3] - 1 - starts[2], &result))
    return false;
  *spec = result;
  return true;
}

/* If the LENGTH bytes at TEXT are a numeric address, put it in SPEC
   and return true.  */
static bool
parse_address (const char *text, size_t length, struct rc_listen_spec *spec)
{
  char address[INET6_ADDRSTRLEN];
  if (length == 0 || length >= sizeof address)
    return false;
  memcpy (address, text, length);
  address[length] = 0;
  spec->family = strchr (address, ':') ? AF_INET6 : AF_INET;
  return inet_pton (spec->family, address, &spec->address) == 1;
}

/* If TEXT is "PORT" or "LOW-HIGH", put the range in SPEC and return
   true.  */
static bool
parse_ports (const char *text, struct rc_listen_spec *spec)
{
  uint16_t low, high;
  const char *end = parse_port (text, &low);
  if (end && *end == 0)
    high = low;
  else if (!end || *end != '-' || !(end = parse_port (end + 1, &high))
	   || *end != 0 || high < low)
    return false;
  spec->low = low;
  spec->high = high;
  return true;
}

/* If TEXT starts with a port in decimal, from 1 to 65535 with no zero
   before it, put it in *PORT and return the end of its digits.
   Otherwise, return NULL.  */
static const char *
parse_port (const char *text, uint16_t *port)
{
  if (*text < '1' || *text > '9')
    return NULL;
  unsigned value = 0;
  for (; *text >= '0' && *text <= '9'; text++)
    {
      value = value * 10 + (*text - '0');
      if (value > 65535)
	return NULL;
    }
  *port = value;
  return text;
}
