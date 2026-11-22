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

#include <config.h>

#include <string.h>

#include "options.h"
#include "progname.h"

/* The column of the help text after a label.  */
enum { HELP_COLUMN = 24 };

/* The defaults in these texts are RC_LINK_DEFAULT_TIMEOUT and
   RC_LINK_PINGS_PER_TIMEOUT of link.h, and RC_LISTEN_LOW and
   RC_LISTEN_HIGH of listenspec.h.  test-options.c checks them.  */
const struct rc_option rc_options[RC_OPTION_COUNT] = {
  { RC_OPTION_HELP, "--help", "-H", RC_OPTION_FLAG, NULL,
    "Print this usage information message, and exit" },
  { RC_OPTION_VERSION, "--version", "-V", RC_OPTION_FLAG, NULL,
    "Print the version of Emacs, and exit" },
  { RC_OPTION_TIMEOUT, "--timeout", NULL, RC_OPTION_JOINED, "MS",
    "Connect again after MS milliseconds with no\n"
    "bytes from the server (default 15000, at least 3)" },
  { RC_OPTION_RENDER_TO, "--render-to", NULL, RC_OPTION_JOINED, "FILE.png",
    "Open no window: when the server closes the\n"
    "connection, write the first frame to FILE.png,\n"
    "and stop" },
  { RC_OPTION_SSH, "--ssh", NULL, RC_OPTION_SEPARATE, "HOST",
    "Start a new session on HOST through SSH, and\n"
    "connect to it through a relay on HOST" },
  { RC_OPTION_SSH_COMMAND, "--ssh-command", NULL, RC_OPTION_JOINED,
    "PROGRAM", "With --ssh, run PROGRAM as SSH (default \"ssh\")" },
  { RC_OPTION_LISTEN, "--listen", NULL, RC_OPTION_JOINED, "SPEC",
    "Let the relay listen at SPEC: ADDRESS,\n"
    "ADDRESS:PORT, or ADDRESS:LOW-HIGH, with an IPv6\n"
    "ADDRESS in brackets (default: the server address\n"
    "of SSH, and the ports 60001-61000)" },
  { RC_OPTION_CONNECT, "--connect", NULL, RC_OPTION_JOINED, "ADDRESS",
    "With --ssh, connect to the relay at ADDRESS\n"
    "(default: the host name that \"ssh -G HOST\" gives)" },
  { RC_OPTION_SESSION, "--session", NULL, RC_OPTION_JOINED, "NAME",
    "With --ssh, resume the open session NAME, and do\n"
    "not start a new session" },
  { RC_OPTION_RELAY, "--relay", NULL, RC_OPTION_FLAG, NULL,
    "Be the relay for the session socket SOCKET.  It\n"
    "must be the first argument.  The client runs\n"
    "\"" RC_PROGRAM " --relay\" on HOST through SSH" },
};

/* The modes of the program, and the environment variables that it
   reads: pairs of a label and a help text, with NULL at the end.  */
static const char *const modes[] = {
  "SOCKET",
  "Connect to the Unix-domain socket SOCKET of a\n"
  "session, and show each frame of the session in a\n"
  "window.  Connect again when the connection ends",
  "--ssh HOST",
  "Start or resume a session on HOST through SSH.\n"
  "The Emacs server on HOST must run",
  "--relay",
  "On the host of the session, connect the session\n"
  "socket and the client",
  "--render-to=FILE.png",
  "Write the first frame to a file, with no display",
  NULL
};
static const char *const environment[] = {
  "SSH_CONNECTION",
  "With --relay and no --listen, the relay listens\n"
  "at the server address in this variable",
  "XDG_CACHE_HOME",
  "The directory of the cache of the fonts (default\n"
  "~/.cache)",
  "DISPLAY, WAYLAND_DISPLAY",
  "The display of the windows",
  NULL
};

static void print_item (FILE *, const char *, const char *);
static void print_items (FILE *, const char *const *);
static void print_option (FILE *, const struct rc_option *);

const struct rc_option *
rc_option_find (const char *text, const char **value)
{
  for (int i = 0; i < RC_OPTION_COUNT; i++)
    {
      const struct rc_option *option = &rc_options[i];
      size_t length = strlen (option->name);
      if (option->form == RC_OPTION_JOINED)
	{
	  if (strncmp (text, option->name, length) == 0
	      && text[length] == '=')
	    {
	      *value = text + length + 1;
	      return option;
	    }
	}
      else if (strcmp (text, option->name) == 0
	       || (option->short_name
		   && strcmp (text, option->short_name) == 0))
	return option;
    }
  return NULL;
}

void
rc_options_help (FILE *stream)
{
  fputs ("Usage: " RC_PROGRAM " [OPTIONS] SOCKET\n"
	 "       " RC_PROGRAM " [OPTIONS] --ssh HOST\n"
	 "       " RC_PROGRAM " --relay [--listen=SPEC] SOCKET\n"
	 "Show the frames of a remote display session of Emacs in"
	 " windows.\n"
	 "\n"
	 "The modes:\n", stream);
  print_items (stream, modes);
  fputs ("\nThe following OPTIONS are accepted:\n", stream);
  for (int i = 0; i < RC_OPTION_COUNT; i++)
    print_option (stream, &rc_options[i]);
  fputs ("\nThe environment:\n", stream);
  print_items (stream, environment);
  fputs ("\nReport bugs with M-x report-emacs-bug.\n", stream);
}

/* Write the label and the help of OPTION to STREAM.  */
static void
print_option (FILE *stream, const struct rc_option *option)
{
  char label[64];
  snprintf (label, sizeof label, "%s%s%s%s%s",
	    option->short_name ? option->short_name : "",
	    option->short_name ? ", " : "", option->name,
	    (option->form == RC_OPTION_JOINED ? "="
	     : option->form == RC_OPTION_SEPARATE ? " " : ""),
	    option->argument ? option->argument : "");
  print_item (stream, label, option->help);
}

/* Write each pair of a label and a help in ITEMS to STREAM.  */
static void
print_items (FILE *stream, const char *const *items)
{
  for (; items[0]; items += 2)
    print_item (stream, items[0], items[1]);
}

/* Write LABEL and HELP to STREAM.  The help starts at HELP_COLUMN, on
   the line of the label if the label is short enough, and each of its
   lines starts at that column.  */
static void
print_item (FILE *stream, const char *label, const char *help)
{
  int length = strlen (label);
  if (length < HELP_COLUMN - 1)
    fprintf (stream, "%s%*s", label, HELP_COLUMN - length, "");
  else
    fprintf (stream, "%s\n%*s", label, HELP_COLUMN, "");
  for (const char *line = help; *line; )
    {
      const char *end = strchr (line, '\n');
      int size = end ? end - line : (int) strlen (line);
      fprintf (stream, "%.*s\n", size, line);
      line += size;
      if (*line)
	{
	  line++;
	  fprintf (stream, "%*s", HELP_COLUMN, "");
	}
    }
}
