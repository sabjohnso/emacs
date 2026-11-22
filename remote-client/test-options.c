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
     name that starts with "--", and a help text.  Only a flag has a
     short name, which is "-" and one letter.  Only a flag has no name
     for its value.
   - unique: no other option has the long name or the short name.
   - find: rc_option_find finds the option from its names, or, for an
     option with a joined value, from "NAME=VALUE" with any VALUE.  It
     gives VALUE for a joined value, and does not change *VALUE for
     another option.
   - not found: rc_option_find finds no option in a text that is nearly
     a name of the option: with one more letter, in the other case,
     without its value, or with a value that it does not have.

   The laws for a random text, from a seed:

   - text: the text is a name of an option with some random changes,
     and perhaps "=" and a value.  rc_option_find agrees with
     find_by_parts, which first cuts the text at its first "=".

   The laws of the help:

   - options: after the line "The following OPTIONS are accepted:" come
     the options of the table, in order, and then an empty line.  Each
     one is its label, and its help text at the column HELP_COLUMN.
   - width: no line has more than WIDTH columns.

   Examples check the start and the end of the help, the defaults of
   --timeout and --listen, and texts that are not options.

   The program runs TRIALS trials of the laws for a random text from a
   seed that it gets from the clock.  With one argument, it runs the
   trial of that seed only.  */

#include <config.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "link.h"
#include "listenspec.h"
#include "options.h"
#include "progname.h"

enum { TRIALS = 20000, HELP_COLUMN = 24, WIDTH = 79, TEXT_SIZE = 256 };

/* The values that the find law gives to an option.  */
static const char *const values[] = {
  "", "x", "a=b", "=", "-x", "--help", "a b", "/a/b.png", "10.0.0.1:1-2",
  "a value that is longer than each name and each help label of the table",
};

/* The line before the options of the help.  */
static const char options_start[]
  = "\nThe following OPTIONS are accepted:\n";

static const char *run_trial (uint64_t);
static bool text_law (uint64_t *);
static bool entry_law (const struct rc_option *);
static bool unique_law (const struct rc_option *);
static bool find_law (const struct rc_option *);
static bool not_found_law (const struct rc_option *);
static bool no_option (const char *, const char *);
static bool options_law (const char *);
static const char *after_option (const struct rc_option *, const char *);
static const char *after_indent (const char *);
static bool width_law (const char *);
static bool help_example (const char *);
static bool find_example (void);
static const struct rc_option *find_by_parts (const char *, const char **);
static void random_text (uint64_t *, char[TEXT_SIZE]);
static char *help_text (void);
static void label (const struct rc_option *, char[TEXT_SIZE]);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

/* A law of an option, and its name.  */
static const struct
{
  const char *name;
  bool (*check) (const struct rc_option *);
} option_laws[] = {
  { "entry", entry_law },
  { "unique", unique_law },
  { "find", find_law },
  { "not found", not_found_law },
};

/* A law or an example of the help, and its name.  */
static const struct
{
  const char *name;
  bool (*check) (const char *);
} help_laws[] = {
  { "options", options_law },
  { "width", width_law },
  { "help", help_example },
};

int
main (int argc, char **argv)
{
  for (int i = 0; i < RC_OPTION_COUNT; i++)
    for (size_t j = 0; j < sizeof option_laws / sizeof *option_laws; j++)
      if (!option_laws[j].check (&rc_options[i]))
	{
	  fprintf (stderr, "test-options: the %s law failed for the option "
		   "%d\n", option_laws[j].name, i);
	  return 1;
	}
  char *help = help_text ();
  if (!help)
    return 1;
  for (size_t j = 0; j < sizeof help_laws / sizeof *help_laws; j++)
    if (!help_laws[j].check (help))
      {
	fprintf (stderr, "test-options: the %s law failed\n",
		 help_laws[j].name);
	return 1;
      }
  free (help);
  if (!find_example ())
    {
      fprintf (stderr, "test-options: the find example failed\n");
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
	  fprintf (stderr, "test-options: the %s law failed with seed "
		   "%llu\n", failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-options: %d options and %d trials passed\n",
	  RC_OPTION_COUNT, TRIALS);
  return 0;
}

/* Run the trial of SEED.  Return the name of the law that fails, or
   NULL if it passes.  */
static const char *
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  return text_law (&state) ? NULL : "text";
}

/* The entry law for OPTION.  The check of the id comes first, because
   the id is an index.  */
static bool
entry_law (const struct rc_option *option)
{
  bool flag = option->form == RC_OPTION_FLAG;
  return ((unsigned int) option->id < RC_OPTION_COUNT
	  && option == &rc_options[option->id]
	  && strncmp (option->name, "--", 2) == 0
	  && islower ((unsigned char) option->name[2])
	  && !strchr (option->name, '=')
	  && option->help && option->help[0]
	  && flag == !option->argument
	  && (!option->short_name
	      || (flag && strlen (option->short_name) == 2
		  && option->short_name[0] == '-'
		  && isalpha ((unsigned char) option->short_name[1]))));
}

/* The unique law for OPTION.  */
static bool
unique_law (const struct rc_option *option)
{
  for (int i = 0; i < RC_OPTION_COUNT; i++)
    {
      const struct rc_option *other = &rc_options[i];
      if (other != option
	  && (strcmp (other->name, option->name) == 0
	      || (other->short_name && option->short_name
		  && strcmp (other->short_name, option->short_name) == 0)))
	return false;
    }
  return true;
}

/* The find law for OPTION.  */
static bool
find_law (const struct rc_option *option)
{
  static const char before[] = "before";
  const char *value = before;
  if (option->form != RC_OPTION_JOINED)
    return (rc_option_find (option->name, &value) == option
	    && (!option->short_name
		|| rc_option_find (option->short_name, &value) == option)
	    && value == before);
  for (size_t i = 0; i < sizeof values / sizeof *values; i++)
    {
      char text[TEXT_SIZE];
      snprintf (text, sizeof text, "%s=%s", option->name, values[i]);
      if (rc_option_find (text, &value) != option
	  || strcmp (value, values[i]) != 0)
	return false;
    }
  return true;
}

/* The not found law for OPTION.  */
static bool
not_found_law (const struct rc_option *option)
{
  char upper[TEXT_SIZE];
  snprintf (upper, sizeof upper, "%s", option->name);
  for (char *p = upper; *p; p++)
    *p = toupper ((unsigned char) *p);
  if (!no_option (option->name, "x") || !no_option (upper, "")
      || !no_option (option->name, "x=1") || !no_option (upper, "=1"))
    return false;
  if (option->form == RC_OPTION_JOINED)
    return no_option (option->name, "");
  if (!no_option (option->name, "=") || !no_option (option->name, "=x"))
    return false;
  if (!option->short_name)
    return true;
  char other[3] = { '-', option->short_name[1], 0 };
  other[1] ^= 'a' ^ 'A';
  return (no_option (option->short_name, "x")
	  && no_option (option->short_name, "=x") && no_option (other, ""));
}

/* Return true if rc_option_find finds no option in the text NAME and
   then REST, and does not change the value.  */
static bool
no_option (const char *name, const char *rest)
{
  static const char before[] = "before";
  const char *value = before;
  char text[TEXT_SIZE];
  snprintf (text, sizeof text, "%s%s", name, rest);
  return !rc_option_find (text, &value) && value == before;
}

/* The text law, for a random text of STATE.  */
static bool
text_law (uint64_t *state)
{
  char text[TEXT_SIZE];
  random_text (state, text);
  const char *value = text, *expected_value = text;
  const struct rc_option *option = rc_option_find (text, &value);
  return (option == find_by_parts (text, &expected_value)
	  && value == expected_value);
}

/* The options law for the help HELP.  */
static bool
options_law (const char *help)
{
  const char *start = strstr (help, options_start);
  if (!start)
    return false;
  const char *p = start + strlen (options_start);
  for (int i = 0; p && i < RC_OPTION_COUNT; i++)
    p = after_option (&rc_options[i], p);
  return p && *p == '\n';
}

/* Return the place after OPTION in the help at P, or NULL if the help
   at P is not OPTION.  The help text is on the line of the label if two
   spaces or more come between them, or else on the next line.  */
static const char *
after_option (const struct rc_option *option, const char *p)
{
  char text[TEXT_SIZE];
  label (option, text);
  size_t length = strlen (text);
  if (strncmp (p, text, length) != 0)
    return NULL;
  p += length;
  if (length > HELP_COLUMN - 2)
    p = *p == '\n' ? after_indent (p + 1) : NULL;
  else if (strspn (p, " ") == HELP_COLUMN - length)
    p += HELP_COLUMN - length;
  else
    return NULL;
  for (const char *rest = option->help; p; )
    {
      size_t size = strcspn (rest, "\n");
      if (size == 0 || strncmp (p, rest, size) != 0 || p[size] != '\n')
	return NULL;
      p += size + 1;
      rest += size;
      if (!*rest++)
	return p;
      p = after_indent (p);
    }
  return NULL;
}

/* Return the place after the spaces at P, or NULL if P does not have
   exactly HELP_COLUMN spaces.  */
static const char *
after_indent (const char *p)
{
  return strspn (p, " ") == HELP_COLUMN ? p + HELP_COLUMN : NULL;
}

/* The width law for the help HELP.  */
static bool
width_law (const char *help)
{
  for (const char *p = help; *p; )
    {
      size_t size = strcspn (p, "\n");
      if (size > WIDTH || p[size] != '\n')
	return false;
      p += size + 1;
    }
  return help[0] != 0;
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
  static const char *const texts[] = {
    "", "-", "--", "---", "=", "-=", "--=", "-h", "-v", "-Hx", "-HV",
    "--no-such-option", "help", "socket", "--Help", " --help", "--help ",
  };
  for (size_t i = 0; i < sizeof texts / sizeof *texts; i++)
    if (!no_option (texts[i], ""))
      return false;
  return true;
}

/* Return the option of TEXT, as rc_option_find.  This reference cuts
   TEXT at its first "=", and then compares the whole parts.  */
static const struct rc_option *
find_by_parts (const char *text, const char **value)
{
  char name[TEXT_SIZE];
  const char *equal = strchr (text, '=');
  snprintf (name, sizeof name, "%.*s",
	    (int) (equal ? (size_t) (equal - text) : strlen (text)), text);
  for (int i = 0; i < RC_OPTION_COUNT; i++)
    {
      const struct rc_option *option = &rc_options[i];
      bool joined = option->form == RC_OPTION_JOINED;
      bool same = (strcmp (name, option->name) == 0
		   || (option->short_name
		       && strcmp (name, option->short_name) == 0));
      if (same && joined == !!equal)
	{
	  if (joined)
	    *value = equal + 1;
	  return option;
	}
    }
  return NULL;
}

/* Put a random text of STATE in TEXT: a name of an option with up to
   three changes, and "=" and a value in one of two cases.  */
static void
random_text (uint64_t *state, char text[TEXT_SIZE])
{
  static const char alphabet[] = "-=HVhvsx eSt";
  const struct rc_option *option
    = &rc_options[random_below (state, RC_OPTION_COUNT)];
  bool is_short = option->short_name && random_below (state, 2);
  snprintf (text, TEXT_SIZE, "%s",
	    is_short ? option->short_name : option->name);
  for (int changes = random_below (state, 4); changes > 0; changes--)
    {
      size_t length = strlen (text);
      size_t at = random_below (state, length + 1);
      char c = alphabet[random_below (state, sizeof alphabet - 1)];
      switch (random_below (state, 3))
	{
	case 0:
	  memmove (text + at + 1, text + at, length - at + 1);
	  text[at] = c;
	  break;
	case 1:
	  if (at < length)
	    text[at] = c;
	  break;
	default:
	  if (at < length)
	    memmove (text + at, text + at + 1, length - at);
	}
    }
  if (random_below (state, 2))
    {
      size_t length = strlen (text);
      snprintf (text + length, TEXT_SIZE - length, "=%s",
		values[random_below (state,
				     sizeof values / sizeof *values)]);
    }
}

/* Put the label of OPTION in TEXT, as the help shows it.  */
static void
label (const struct rc_option *option, char text[TEXT_SIZE])
{
  snprintf (text, TEXT_SIZE, "%s%s%s%s%s",
	    option->short_name ? option->short_name : "",
	    option->short_name ? ", " : "", option->name,
	    (option->form == RC_OPTION_JOINED ? "="
	     : option->form == RC_OPTION_SEPARATE ? " " : ""),
	    option->argument ? option->argument : "");
}

/* Return the help text in a new string, or NULL with a message.  */
static char *
help_text (void)
{
  char *text = NULL;
  size_t size = 0;
  FILE *stream = open_memstream (&text, &size);
  if (stream)
    {
      rc_options_help (stream);
      if (fclose (stream) == 0)
	return text;
    }
  perror ("test-options");
  free (text);
  return NULL;
}

/* Return a random number of STATE that is less than LIMIT.  */
static uint64_t
random_below (uint64_t *state, uint64_t limit)
{
  return random_next (state) % limit;
}

/* Return the next random number of STATE, by splitmix64.  */
static uint64_t
random_next (uint64_t *state)
{
  uint64_t z = *state += 0x9e3779b97f4a7c15;
  z = (z ^ z >> 30) * 0xbf58476d1ce4e5b9;
  z = (z ^ z >> 27) * 0x94d049bb133111eb;
  return z ^ z >> 31;
}
