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

#include <config.h>

#include <string.h>

#include "sshconfig.h"

bool
rc_ssh_config_hostname (const char *text, char *name, size_t name_size)
{
  static const char key[] = "hostname ";
  for (const char *line = text; *line; )
    {
      size_t length = strcspn (line, "\n");
      if (strncmp (line, key, sizeof key - 1) == 0)
	{
	  const char *value = line + sizeof key - 1;
	  size_t value_length = length - (sizeof key - 1);
	  if (value_length == 0 || value_length >= name_size
	      || strcspn (value, " \t\r\n") != value_length)
	    return false;
	  memcpy (name, value, value_length);
	  name[value_length] = 0;
	  return true;
	}
      line += length + (line[length] == '\n');
    }
  return false;
}
