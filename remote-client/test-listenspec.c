/* Test the listen spec of the relay of remote-client.

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

/* The laws of the listen spec.

   - Round trip: rc_listen_spec_parse of the format of a spec gives the
     spec again.

   - Safe: the format of a spec has only the characters of SAFE, and
     fits in RC_LISTEN_TEXT_SIZE bytes.

   - Short forms: the text of the address alone gives the default
     range, and "ADDRESS:PORT" gives the range from PORT to PORT.  An
     IPv6 address alone can be with or with no brackets.

   - Keep: when rc_listen_spec_parse refuses a text, it does not change
     the spec.  The text is the format of a random spec with one to
     four changes, or random bytes, so most texts are not valid.

   - Canonical: when rc_listen_spec_parse accepts such a text, the
     format of the result parses to the same spec.

   - Address: the socket address of a spec and a port has the family,
     the port, and the address of the spec, and the size of its
     family.

   - Connection: rc_listen_spec_from_connection of the text of a random
     connection gives the server address with the default range.  When
     it refuses a changed text, it does not change the spec.

   The examples:

   - Golden: known texts give known specs, and the format of a known
     spec is a known text.
   - Reject: the parse refuses an empty text, a host name, an address
     with no port after the colon, a port 0, a port larger than 65535,
     a port with a zero before it, a sign before a port, a range from a
     high port to a low port, a range with no high port, three ports,
     an IPv4 address in brackets, an IPv6 address with no closing
     bracket, a space after the text, and a newline after the text.
   - Connection: known values of $SSH_CONNECTION give known specs, and
     the function refuses three fields, five fields, a host name, two
     spaces, a space before the text, and a space after it.  It checks
     only the server address, so other fields that are not addresses
     or ports are accepted.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "listenspec.h"

enum { TRIALS = 1000 };

/* The characters that a shell does not change.  */
static const char SAFE[] = "0123456789abcdef.:[]-";

static const char *run_trial (uint64_t);
static bool round_trip_law (uint64_t *);
static bool safe_law (uint64_t *);
static bool short_forms_law (uint64_t *);
static bool keep_law (uint64_t *);
static bool canonical_law (uint64_t *);
static bool connection_law (uint64_t *);
static bool address_law (uint64_t *);
static bool golden_example (void);
static bool reject_example (void);
static bool connection_example (void);
static bool parses_to (const char *, const char *, uint16_t, uint16_t);
static bool connects_to (const char *, const char *);
static struct rc_listen_spec random_spec (uint64_t *);
static void random_address (uint64_t *, struct rc_listen_spec *);
static void address_text (const struct rc_listen_spec *, char *, size_t);
static void random_text (uint64_t *, char *, size_t);
static void change (uint64_t *, char *);
static bool same_spec (const struct rc_listen_spec *,
		       const struct rc_listen_spec *);
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
    { "safe", safe_law },
    { "short forms", short_forms_law },
    { "keep", keep_law },
    { "canonical", canonical_law },
    { "connection", connection_law },
    { "address", address_law },
  };

/* An example and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} examples[] =
  {
    { "golden", golden_example },
    { "reject", reject_example },
    { "connection", connection_example },
  };

int
main (int argc, char **argv)
{
  for (size_t i = 0; i < sizeof examples / sizeof *examples; i++)
    if (!examples[i].check ())
      {
	fprintf (stderr, "test-listenspec: the %s example failed\n",
		 examples[i].name);
	return 1;
      }
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 1 : 0;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    {
      const char *failed = run_trial (base + trial);
      if (failed)
	{
	  fprintf (stderr,
		   "test-listenspec: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-listenspec: %d trials passed\n", TRIALS);
  return 0;
}

/* Run the trial of SEED: one case of each law.  Return the name of the
   first law that fails, or NULL if each law passes.  */
static const char *
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  for (size_t i = 0; i < sizeof laws / sizeof *laws; i++)
    if (!laws[i].check (&state))
      return laws[i].name;
  return NULL;
}

/* The round trip law, for a random spec of STATE.  */
static bool
round_trip_law (uint64_t *state)
{
  struct rc_listen_spec spec = random_spec (state), parsed;
  char text[RC_LISTEN_TEXT_SIZE];
  rc_listen_spec_format (&spec, text);
  return rc_listen_spec_parse (text, &parsed) && same_spec (&spec, &parsed);
}

/* The safe law, for a random spec of STATE.  */
static bool
safe_law (uint64_t *state)
{
  struct rc_listen_spec spec = random_spec (state);
  char text[RC_LISTEN_TEXT_SIZE + 1];
  memset (text, 'x', sizeof text);
  rc_listen_spec_format (&spec, text);
  return text[RC_LISTEN_TEXT_SIZE] == 'x'
    && memchr (text, 0, RC_LISTEN_TEXT_SIZE)
    && strspn (text, SAFE) == strlen (text);
}

/* The short forms law, for a random spec of STATE.  */
static bool
short_forms_law (uint64_t *state)
{
  struct rc_listen_spec spec = random_spec (state), parsed;
  char address[INET6_ADDRSTRLEN], text[2 * RC_LISTEN_TEXT_SIZE];
  address_text (&spec, address, sizeof address);
  bool v6 = spec.family == AF_INET6;

  struct rc_listen_spec wide = spec;
  wide.low = RC_LISTEN_LOW;
  wide.high = RC_LISTEN_HIGH;
  if (!rc_listen_spec_parse (address, &parsed) || !same_spec (&wide, &parsed))
    return false;
  snprintf (text, sizeof text, "[%s]", address);
  if (v6 != rc_listen_spec_parse (text, &parsed)
      || (v6 && !same_spec (&wide, &parsed)))
    return false;

  struct rc_listen_spec one = spec;
  one.high = one.low;
  snprintf (text, sizeof text, v6 ? "[%s]:%u" : "%s:%u", address,
	    (unsigned) one.low);
  return rc_listen_spec_parse (text, &parsed) && same_spec (&one, &parsed);
}

/* The keep law, for a random text of STATE.  */
static bool
keep_law (uint64_t *state)
{
  char text[2 * RC_LISTEN_TEXT_SIZE];
  random_text (state, text, sizeof text);
  struct rc_listen_spec spec = random_spec (state), copy = spec;
  return rc_listen_spec_parse (text, &copy) || same_spec (&spec, &copy);
}

/* The canonical law, for a random text of STATE.  */
static bool
canonical_law (uint64_t *state)
{
  char text[2 * RC_LISTEN_TEXT_SIZE], format[RC_LISTEN_TEXT_SIZE];
  random_text (state, text, sizeof text);
  struct rc_listen_spec spec, again;
  if (!rc_listen_spec_parse (text, &spec))
    return true;
  rc_listen_spec_format (&spec, format);
  return rc_listen_spec_parse (format, &again) && same_spec (&spec, &again);
}

/* The connection law, for a random connection of STATE.  */
static bool
connection_law (uint64_t *state)
{
  struct rc_listen_spec client = random_spec (state);
  struct rc_listen_spec server = random_spec (state), parsed;
  char client_text[INET6_ADDRSTRLEN], server_text[INET6_ADDRSTRLEN];
  char text[4 * INET6_ADDRSTRLEN];
  address_text (&client, client_text, sizeof client_text);
  address_text (&server, server_text, sizeof server_text);
  snprintf (text, sizeof text, "%s %u %s %u", client_text,
	    (unsigned) client.low, server_text, (unsigned) server.low);
  server.low = RC_LISTEN_LOW;
  server.high = RC_LISTEN_HIGH;
  if (!rc_listen_spec_from_connection (text, &parsed)
      || !same_spec (&server, &parsed))
    return false;
  for (int changes = 1 + random_below (state, 4); changes > 0; changes--)
    change (state, text);
  struct rc_listen_spec copy = client;
  return rc_listen_spec_from_connection (text, &copy)
    || same_spec (&client, &copy);
}

/* The address law, for a random spec and port of STATE.  */
static bool
address_law (uint64_t *state)
{
  struct rc_listen_spec spec = random_spec (state);
  uint16_t port = 1 + random_below (state, 65535);
  struct sockaddr_storage address;
  memset (&address, 0xa5, sizeof address);
  socklen_t size = rc_listen_spec_address (&spec, port, &address);
  if (address.ss_family != spec.family)
    return false;
  if (spec.family == AF_INET)
    {
      struct sockaddr_in *v4 = (struct sockaddr_in *) &address;
      return size == sizeof *v4 && ntohs (v4->sin_port) == port
	&& memcmp (&v4->sin_addr, &spec.address.v4, sizeof v4->sin_addr) == 0;
    }
  struct sockaddr_in6 *v6 = (struct sockaddr_in6 *) &address;
  return size == sizeof *v6 && ntohs (v6->sin6_port) == port
    && memcmp (&v6->sin6_addr, &spec.address.v6, sizeof v6->sin6_addr) == 0
    && v6->sin6_flowinfo == 0 && v6->sin6_scope_id == 0;
}

/* The example of known texts and specs.  */
static bool
golden_example (void)
{
  static const struct
  {
    const char *text, *address;
    uint16_t low, high;
    const char *format;
  } cases[] =
    {
      { "127.0.0.1:60001-61000", "127.0.0.1", 60001, 61000,
	"127.0.0.1:60001-61000" },
      { "10.0.0.200", "10.0.0.200", RC_LISTEN_LOW, RC_LISTEN_HIGH,
	"10.0.0.200:60001-61000" },
      { "172.29.132.59:80", "172.29.132.59", 80, 80,
	"172.29.132.59:80-80" },
      { "0.0.0.0:1-65535", "0.0.0.0", 1, 65535, "0.0.0.0:1-65535" },
      { "[::1]:1-9", "::1", 1, 9, "[::1]:1-9" },
      { "::1", "::1", RC_LISTEN_LOW, RC_LISTEN_HIGH,
	"[::1]:60001-61000" },
      { "[fe80::215:5dff:fede:9b3]:65535", "fe80::215:5dff:fede:9b3",
	65535, 65535, "[fe80::215:5dff:fede:9b3]:65535-65535" },
      { "[0:0:0:0:0:0:0:1]", "::1", RC_LISTEN_LOW, RC_LISTEN_HIGH,
	"[::1]:60001-61000" },
    };
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
    {
      struct rc_listen_spec spec;
      char text[RC_LISTEN_TEXT_SIZE];
      if (!parses_to (cases[i].text, cases[i].address, cases[i].low,
		      cases[i].high)
	  || !rc_listen_spec_parse (cases[i].text, &spec))
	return false;
      rc_listen_spec_format (&spec, text);
      if (strcmp (text, cases[i].format) != 0)
	return false;
    }
  return true;
}

/* The example of the texts that the parse refuses.  */
static bool
reject_example (void)
{
  static const char *const texts[] =
    {
      "", "localhost", "127.0.0.1:", ":80", "127.0.0.1:0",
      "127.0.0.1:65536", "127.0.0.1:080", "127.0.0.1:+80",
      "127.0.0.1:90-80", "127.0.0.1:80-", "127.0.0.1:80-90-100",
      "[127.0.0.1]:80", "[127.0.0.1]", "[::1", "[::1]:", "::1]",
      "127.0.0.1 ", "127.0.0.1:80\n", "1.2.3", "[::1]80",
      "127.0.0.1:-80",
    };
  for (size_t i = 0; i < sizeof texts / sizeof *texts; i++)
    {
      struct rc_listen_spec spec;
      if (rc_listen_spec_parse (texts[i], &spec))
	return false;
    }
  return true;
}

/* The example of known values of $SSH_CONNECTION.  */
static bool
connection_example (void)
{
  static const char *const rejected[] =
    {
      "", "10.0.0.1 5000 172.29.132.59",
      "10.0.0.1 5000 172.29.132.59 22 x",
      "10.0.0.1 5000 workstation 22",
      "10.0.0.1 5000  172.29.132.59 22",
      " 10.0.0.1 5000 172.29.132.59 22", "10.0.0.1 5000 172.29.132.59 22 ",
    };
  for (size_t i = 0; i < sizeof rejected / sizeof *rejected; i++)
    {
      struct rc_listen_spec spec;
      if (rc_listen_spec_from_connection (rejected[i], &spec))
	return false;
    }
  return connects_to ("172.29.128.1 49464 172.29.132.59 22",
		      "172.29.132.59")
    && connects_to ("fe80::1 5 ::1 22", "::1")
    && connects_to ("a b 10.0.0.1 c", "10.0.0.1");
}

/* Return true if TEXT parses to the spec of ADDRESS, LOW, and HIGH.  */
static bool
parses_to (const char *text, const char *address, uint16_t low,
	   uint16_t high)
{
  struct rc_listen_spec expected = { .low = low, .high = high }, spec;
  expected.family = strchr (address, ':') ? AF_INET6 : AF_INET;
  return inet_pton (expected.family, address, &expected.address) == 1
    && rc_listen_spec_parse (text, &spec) && same_spec (&expected, &spec);
}

/* Return true if CONNECTION gives the spec of ADDRESS with the default
   range.  */
static bool
connects_to (const char *connection, const char *address)
{
  struct rc_listen_spec spec;
  char text[INET6_ADDRSTRLEN + sizeof "[]:60001-61000"];
  snprintf (text, sizeof text, strchr (address, ':') ? "[%s]" : "%s",
	    address);
  return rc_listen_spec_from_connection (connection, &spec)
    && parses_to (text, address, RC_LISTEN_LOW, RC_LISTEN_HIGH)
    && spec.low == RC_LISTEN_LOW && spec.high == RC_LISTEN_HIGH;
}

/* Return a random spec of STATE.  */
static struct rc_listen_spec
random_spec (uint64_t *state)
{
  static const uint16_t edges[] = { 1, 9, 10, 99, 100, 60001, 61000, 65535 };
  struct rc_listen_spec spec;
  random_address (state, &spec);
  uint16_t ports[2];
  for (int i = 0; i < 2; i++)
    ports[i] = (random_below (state, 3) == 0
		? edges[random_below (state, sizeof edges / sizeof *edges)]
		: 1 + random_below (state, 65535));
  spec.low = ports[0] < ports[1] ? ports[0] : ports[1];
  spec.high = ports[0] < ports[1] ? ports[1] : ports[0];
  return spec;
}

/* Put a random address of STATE in SPEC.  Some IPv6 addresses have
   runs of zeros, and some hold an IPv4 address.  */
static void
random_address (uint64_t *state, struct rc_listen_spec *spec)
{
  memset (&spec->address, 0, sizeof spec->address);
  if (random_below (state, 2) == 0)
    {
      spec->family = AF_INET;
      uint32_t bits = random_next (state);
      memcpy (&spec->address.v4, &bits, sizeof bits);
      return;
    }
  spec->family = AF_INET6;
  uint8_t *bytes = spec->address.v6.s6_addr;
  switch (random_below (state, 3))
    {
    case 0:
      for (int i = 0; i < 16; i++)
	bytes[i] = random_below (state, 256);
      break;
    case 1:
      for (int i = 0; i < 16; i += 2)
	if (random_below (state, 2) == 0)
	  bytes[i + random_below (state, 2)] = random_below (state, 256);
      break;
    default:
      bytes[10] = bytes[11] = 0xff;
      for (int i = 12; i < 16; i++)
	bytes[i] = random_below (state, 256);
      break;
    }
}

/* Write the address of SPEC with no brackets to TEXT, a buffer of SIZE
   bytes.  */
static void
address_text (const struct rc_listen_spec *spec, char *text, size_t size)
{
  inet_ntop (spec->family, &spec->address, text, size);
}

/* Put a random text of STATE in TEXT, a buffer of SIZE bytes.  Half of
   the texts are random bytes.  The other half are the format of a
   random spec with one to four changes.  */
static void
random_text (uint64_t *state, char *text, size_t size)
{
  if (random_below (state, 2) == 0)
    {
      size_t length = random_below (state, RC_LISTEN_TEXT_SIZE + 4);
      for (size_t i = 0; i < length; i++)
	text[i] = 1 + random_below (state, 255);
      text[length] = 0;
      return;
    }
  struct rc_listen_spec spec = random_spec (state);
  char format[RC_LISTEN_TEXT_SIZE];
  rc_listen_spec_format (&spec, format);
  snprintf (text, size, "%s", format);
  for (int changes = 1 + random_below (state, 4); changes > 0; changes--)
    change (state, text);
}

/* Replace, delete, or insert one random byte of TEXT.  A null byte
   cuts the text.  TEXT has room for four more bytes.  */
static void
change (uint64_t *state, char *text)
{
  static const char bytes[] = "0123456789abcdefgABCDEF .:[]-+x\n";
  size_t length = strlen (text);
  size_t at = random_below (state, length + 1);
  char byte = random_below (state, 16) == 0
    ? 0 : bytes[random_below (state, sizeof bytes - 1)];
  switch (random_below (state, 3))
    {
    case 0:
      if (at < length)
	text[at] = byte;
      break;
    case 1:
      if (at < length)
	memmove (text + at, text + at + 1, length - at);
      break;
    default:
      memmove (text + at + 1, text + at, length - at + 1);
      text[at] = byte;
      break;
    }
}

/* Return true if the specs A and B are equal.  */
static bool
same_spec (const struct rc_listen_spec *a, const struct rc_listen_spec *b)
{
  size_t size = (a->family == AF_INET ? sizeof a->address.v4
		 : sizeof a->address.v6);
  return a->family == b->family && a->low == b->low && a->high == b->high
    && memcmp (&a->address, &b->address, size) == 0;
}

/* Return a random number from 0 to LIMIT - 1.  */
static uint64_t
random_below (uint64_t *state, uint64_t limit)
{
  return random_next (state) % limit;
}

/* Return the next random number of STATE, with splitmix64.  */
static uint64_t
random_next (uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}
