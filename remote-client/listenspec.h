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

/* A listen spec tells the relay where to listen: one numeric IPv4 or
   IPv6 address, and a range of ports.  The relay listens on the first
   free port of the range.  The user selects the spec with this text:

     ADDRESS          the default range
     ADDRESS:PORT     only PORT
     ADDRESS:LOW-HIGH the ports from LOW to HIGH

   An IPv6 address with ports is in brackets, for example
   "[::1]:60001-60010".  A port is in decimal, from 1 to 65535, with no
   zero before it, and LOW is not larger than HIGH.

   The format of a spec is "ADDRESS:LOW-HIGH", or "[ADDRESS]:LOW-HIGH"
   for IPv6.  It has only digits, the letters a to f, and the
   characters ".:[]-", so a shell does not change it.  Thus the client
   can put it in the remote command of SSH.  */

#ifndef REMOTE_CLIENT_LISTENSPEC_H
#define REMOTE_CLIENT_LISTENSPEC_H

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

/* The default range of ports.  mosh uses the same range.  */
enum { RC_LISTEN_LOW = 60001, RC_LISTEN_HIGH = 61000 };

/* The size of a buffer for the format of a spec, with its null
   byte.  */
enum { RC_LISTEN_TEXT_SIZE = INET6_ADDRSTRLEN + sizeof "[]:65535-65535" };

/* FAMILY is AF_INET or AF_INET6, and selects the address.  */
struct rc_listen_spec
{
  int family;
  union
  {
    struct in_addr v4;
    struct in6_addr v6;
  } address;
  uint16_t low, high;
};

/* If TEXT is the text of a spec, put the spec in SPEC and return true.
   Otherwise, return false, and do not change SPEC.  */
extern bool rc_listen_spec_parse (const char *text,
				  struct rc_listen_spec *spec);

/* Write the format of SPEC and a null byte to TEXT.  */
extern void rc_listen_spec_format (const struct rc_listen_spec *spec,
				   char text[RC_LISTEN_TEXT_SIZE]);

/* Put the socket address of PORT at the address of SPEC in ADDRESS,
   and return its size.  */
extern socklen_t rc_listen_spec_address (const struct rc_listen_spec *spec,
					 uint16_t port,
					 struct sockaddr_storage *address);

/* CONNECTION is the value of $SSH_CONNECTION: the client address, the
   client port, the server address, and the server port, with one space
   between two fields.  The function checks only that the server
   address is a numeric address.  If it is valid, put a spec of the
   server address and the default range in SPEC, and return true.
   Otherwise, return false, and do not change SPEC.  */
extern bool rc_listen_spec_from_connection (const char *connection,
					    struct rc_listen_spec *spec);

#endif /* REMOTE_CLIENT_LISTENSPEC_H */
