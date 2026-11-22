/* The options and the help of the command line of the client.

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

/* The table rc_options has each option of the program, with its help.
   The parser of client.c finds the options in the table, and the help
   shows each option of the table.  Thus an option without help cannot
   be on the command line.  doc/man/emacs-remote-client.1 has the same
   options with the same values, and check-man.sh checks this.  */

#ifndef REMOTE_CLIENT_OPTIONS_H
#define REMOTE_CLIENT_OPTIONS_H

#include <stdio.h>

enum rc_option_id
{
  RC_OPTION_HELP,
  RC_OPTION_VERSION,
  RC_OPTION_TIMEOUT,
  RC_OPTION_RENDER_TO,
  RC_OPTION_SSH,
  RC_OPTION_SSH_COMMAND,
  RC_OPTION_LISTEN,
  RC_OPTION_CONNECT,
  RC_OPTION_SESSION,
  RC_OPTION_RELAY,
  RC_OPTION_COUNT
};

/* How an option gets its value: no value, "--NAME=VALUE", or
   "--NAME VALUE".  */
enum rc_option_form
{
  RC_OPTION_FLAG,
  RC_OPTION_JOINED,
  RC_OPTION_SEPARATE
};

/* NAME is the long name, for example "--timeout".  SHORT_NAME is NULL
   or the short name, for example "-H".  ARGUMENT is the name of the
   value in the help, or NULL for a flag.  HELP is the help text.  */
struct rc_option
{
  enum rc_option_id id;
  const char *name;
  const char *short_name;
  enum rc_option_form form;
  const char *argument;
  const char *help;
};

extern const struct rc_option rc_options[RC_OPTION_COUNT];

/* Return the option of the argument TEXT of the command line, or NULL
   if TEXT is not an option.  For an option with a joined value, TEXT
   is "--NAME=VALUE": put VALUE in *VALUE.  */
extern const struct rc_option *rc_option_find (const char *text,
					       const char **value);

/* Write the help of the program to STREAM.  */
extern void rc_options_help (FILE *stream);

#endif /* REMOTE_CLIENT_OPTIONS_H */
