/* The configuration that "ssh -G" gives, for remote-client.

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

/* "ssh -G HOST" writes the configuration of HOST, one "KEY VALUE" line
   for each key, and stops.  It does not connect.  The "hostname" line
   holds the name or the address that SSH connects to, also when HOST
   is an alias of the configuration.  The client connects to the relay
   at that name.  */

#ifndef REMOTE_CLIENT_SSHCONFIG_H
#define REMOTE_CLIENT_SSHCONFIG_H

#include <stdbool.h>
#include <stddef.h>

/* The size of a buffer for the output of "ssh -G".  An output of
   OpenSSH 9 has about 4000 bytes.  */
enum { RC_SSH_CONFIG_SIZE = 65536 };

/* The size of a buffer for a host name, with its null byte.  */
enum { RC_SSH_HOSTNAME_SIZE = 1025 };

/* Find the first line "hostname NAME" of TEXT, the output of
   "ssh -G".  If NAME is not empty, has no space, tab, or carriage
   return, and fits in NAME_SIZE bytes with a null byte, put it and the
   null byte in NAME, and return true.  Otherwise, return false, and
   do not change NAME.  */
extern bool rc_ssh_config_hostname (const char *text, char *name,
				    size_t name_size);

#endif /* REMOTE_CLIENT_SSHCONFIG_H */
