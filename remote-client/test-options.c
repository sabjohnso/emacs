/* Test the options and the help of the command line of the client.

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

/* The laws, for each option of the table rc_options:

   - entry: the option has the place of its id in the table, a long
     name that starts with "--", and a help text.
   - find: rc_option_find finds the option from its long name, from its
     short name, and from "--NAME=VALUE" for an option with a joined
     value.  It gives VALUE.  No other option has the same text, so
     the find gives the option itself and not a longer or a shorter
     one.
   - help: the help text has the label of the option ("-H, --help",
     "--timeout=MS", "--ssh HOST") at the start of a line.

   Examples check the start and the end of the help, the defaults of
   --timeout and --listen, and texts that are not options.  */

#include <config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link.h"
#include "listenspec.h"
#include "options.h"
#include "progname.h"

static bool entry_law (const struct rc_option *);
static bool find_law (const struct rc_option *);
static bool help_law (const struct rc_option *, const char *);
static bool help_example (const char *);
static bool find_example (void);
static char *help_text (void);
static void label (const struct rc_option *, char *, size_t);

int
main (void)
{
  char *help = help_text ();
  bool ok = help && help_example (help) && find_example ();
  if (!ok)
    fprintf (stderr, "test-options: an example failed\n");
  for (int i = 0; ok && i < RC_OPTION_COUNT; i++)
    {
      const struct rc_option *option = &rc_options[i];
      ok = (entry_law (option) && find_law (option)
	    && help_law (option, help));
      if (!ok)
	fprintf (stderr, "test-options: a law failed for the option %d\n",
		 i);
    }
  free (help);
  return ok ? 0 : 1;
}

/* Return true if OPTION has the place of its id, a long name, and a
   help text.  */
static bool
entry_law (const struct rc_option *option)
{
  return (option == &rc_options[option->id]
	  && strncmp (option->name, "--", 2) == 0 && option->name[2]
	  && option->help && option->help[0]
	  && (option->form == RC_OPTION_FLAG) == !option->argument);
}

/* Return true if rc_option_find gives OPTION for each of its forms.  */
static bool
find_law (const struct rc_option *option)
{
  const char *value = NULL;
  if (option->form == RC_OPTION_JOINED)
    {
      char text[64];
      snprintf (text, sizeof text, "%s=value-of-test", option->name);
      return (rc_option_find (text, &value) == option && value
	      && strcmp (value, "value-of-test") == 0
	      && !rc_option_find (option->name, &value));
    }
  if (rc_option_find (option->name, &value) != option)
    return false;
  return (!option->short_name
	  || rc_option_find (option->short_name, &value) == option);
}

/* Return true if the help HELP has the label of OPTION at the start of
   a line.  */
static bool
help_law (const struct rc_option *option, const char *help)
{
  char text[96];
  label (option, text + 1, sizeof text - 1);
  text[0] = '\n';
  return strstr (help, text) != NULL;
}

/* Put in TEXT, of SIZE bytes, the label of OPTION as the help shows
   it.  */
static void
label (const struct rc_option *option, char *text, size_t size)
{
  char prefix[16] = "";
  if (option->short_name)
    snprintf (prefix, sizeof prefix, "%s, ", option->short_name);
  snprintf (text, size, "%s%s%s%s", prefix, option->name,
	    (option->form == RC_OPTION_JOINED ? "="
	     : option->form == RC_OPTION_SEPARATE ? " " : ""),
	    option->argument ? option->argument : "");
}

/* Return true if the help HELP has the usage line at the start, the
   defaults of --timeout and --listen, and the line about bugs at the
   end.  */
static bool
help_example (const char *help)
{
  char timeout[64], ports[32];
  snprintf (timeout, sizeof timeout, "(default %d, at least %d)",
	    RC_LINK_DEFAULT_TIMEOUT, RC_LINK_PINGS_PER_TIMEOUT);
  snprintf (ports, sizeof ports, "ports %d-%d)", RC_LISTEN_LOW,
	    RC_LISTEN_HIGH);
  const char *usage = "Usage: " RC_PROGRAM " [OPTIONS] SOCKET\n";
  const char *bugs = "\nReport bugs with M-x report-emacs-bug.\n";
  size_t length = strlen (help);
  return (strncmp (help, usage, strlen (usage)) == 0
	  && strstr (help, timeout) && strstr (help, ports)
	  && length > strlen (bugs)
	  && strcmp (help + length - strlen (bugs), bugs) == 0);
}

/* Return true if rc_option_find finds no option in texts that are not
   options.  */
static bool
find_example (void)
{
  const char *value;
  return (!rc_option_find ("--no-such-option", &value)
	  && !rc_option_find ("--", &value)
	  && !rc_option_find ("-", &value)
	  && !rc_option_find ("--sshx", &value)
	  && !rc_option_find ("--helpx", &value)
	  && !rc_option_find ("socket", &value));
}

/* Return the help text in a new string, or NULL.  */
static char *
help_text (void)
{
  char *text = NULL;
  size_t size = 0;
  FILE *stream = open_memstream (&text, &size);
  if (!stream)
    return NULL;
  rc_options_help (stream);
  if (fclose (stream) != 0)
    {
      free (text);
      return NULL;
    }
  return text;
}
