/* Tests of the program emacs-remote-client.

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

/* The test starts "emacs-remote-client --render-to=FILE.png SOCKET"
   with a socket of the test, and acts as the server.  The laws:

   - render-to: after the server closes the connection, the program
     writes the surface of the first frame that the server defined to
     FILE.png, and exits with the status 0.  The frame can have a new
     size.  The pixels of the file are
     the pixels of a renderer that got the same messages.  The
     chunks of the stream do not change the pixels.
   - children: FILE.png has the composite of the first root frame: the
     pixels of a visible child frame are on the pixels of the root, in
     the area of the child that is in the root.  The first root can
     come after the define-frame of its child.
   - error: after a frame with an error, the program exits with the
     status 1, and writes no file.
   - no frame: if the server defined no frame, or the first frame does
     not exist at the end, the program exits with the status 1, and
     writes no file.
   - cut: a connection that closes in a frame gives the pixels of the
     messages before that frame, and the status 0.
   - fonts: a define-font with the hash of a font file of the system
     draws with that file.  A define-font with no hash or a short hash
     draws with the fallback font.  A define-font with a full hash of
     no file draws nothing.  The test uses the fontconfig match of
     "sans:bold", which is not the fallback font.
   - cache: the program writes its cache of font hashes in
     $XDG_CACHE_HOME/remote-client/font-hashes, and a second run with
     that cache gives the same pixels.  The test sets $XDG_CACHE_HOME
     to the directory of the run, not to the cache directory of the
     user.

   The trials do not need a display.  With no argument, the program
   runs the error example and TRIALS trials from a seed that it gets
   from the clock and the process ID.  With the argument SEED, it runs
   only the trial of that seed.  */

#include <config.h>

#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <glib.h>

#include "fonts.h"
#include "hashfonts.h"
#include "rdproto.h"
#include "render.h"

enum { TRIALS = 20, STEPS = 60 };

/* A long trial also sends LONG_STEPS draws of RD_MAX_GLYPHS glyphs.
   Their bytes are more than the program reads in one call.  */
enum { LONG_STEPS = 64 };

/* The longest wait for the connection of the program, and for its
   exit, in milliseconds.  */
enum { CONNECT_TIMEOUT = 10000, EXIT_TIMEOUT = 30000 };

/* The frame IDs of a trial.  */
enum { FRAME_A = 5, FRAME_B = 6 };

/* The exit status of a child that could not start the program.  */
enum { NOT_STARTED = 127 };

/* The font file of the fonts law, and its hash.  */
static char *system_font;
static uint8_t system_hash[RD_HASH_SIZE];

/* The files of one run of the program.  */
struct run
{
  char directory[64];
  char socket[100];
  char png[100];
};

static bool error_example (void);
static bool children_example (bool);
static bool composite_p (const char *);
static bool png_pixel (cairo_surface_t *, int, int, uint32_t);
static bool run_trial (uint64_t);
static int run_client (struct run *, const struct rd_message *, int,
		       const unsigned char *, size_t, uint64_t *);
static bool find_system_font (void);
static bool fonts_example (void);
static bool change_cache_hash (const char *, const char *);
static void ignore_fallback (const struct rc_font_key *);
static int wait_for_exit (pid_t);
static bool first_frame (const struct rd_message *, int, uint32_t *);
static bool same_as_renderer (const char *, const struct rd_message *,
			      int, uint32_t);
static bool same_as_unfound (const char *, const struct rd_message *,
			     int, uint32_t);
static bool same_pixels (const char *, const struct rd_message *, int,
			 uint32_t, struct rc_font_source);
static void send_in_chunks (int, const unsigned char *, size_t,
			    uint64_t *);
static int listen_at (const char *);
static void make_run (struct run *);
static void remove_run (const struct run *);
static struct rd_message random_message (uint64_t *, bool, bool);
static struct rd_message random_long_draw (uint64_t *);
static void random_cut (uint64_t *, unsigned char *, size_t *);
static void random_bad_frame (uint64_t *, unsigned char *, size_t *);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);

int
main (int argc, char **argv)
{
  if (!find_system_font ())
    {
      fprintf (stderr, "test-client: no match for \"sans:bold\"\n");
      return 1;
    }
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 0 : 1;
  if (!error_example () || !fonts_example () || !children_example (false)
      || !children_example (true))
    return 1;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    if (!run_trial (base + trial))
      {
	fprintf (stderr, "test-client: seed %llu failed\n",
		 (unsigned long long) (base + trial));
	return 1;
      }
  printf ("test-client: the examples and %d trials passed\n", TRIALS);
  return 0;
}

/* Set SYSTEM_FONT to the file of the fontconfig match of "sans:bold",
   and SYSTEM_HASH to its hash.  Return false if fontconfig has no
   match.  */
static bool
find_system_font (void)
{
  FcPattern *pattern = FcNameParse ((const FcChar8 *) "sans:bold");
  FcConfigSubstitute (NULL, pattern, FcMatchPattern);
  FcDefaultSubstitute (pattern);
  FcResult result;
  FcPattern *match = FcFontMatch (NULL, pattern, &result);
  FcPatternDestroy (pattern);
  FcChar8 *file;
  bool found = (match && FcPatternGetString (match, FC_FILE, 0, &file)
		== FcResultMatch);
  if (found)
    system_font = strdup ((const char *) file);
  if (match)
    FcPatternDestroy (match);
  return found && rc_sha256_file (NULL, system_font, system_hash);
}

/* The fonts law and the cache law: the same glyphs with the font 0,
   which has the hash of SYSTEM_FONT, with the font 1, which has no
   hash, with the font 2, which has a short hash, and with the font 3,
   which has a hash of no file.  The expected pixels must not be the
   pixels with no font file, so the example shows that the program
   found SYSTEM_FONT.  The second run uses the cache of the first.
   Then the line of SYSTEM_FONT in the cache file gets another hash,
   and the third run must not draw the font 0, so the example shows
   that the program reads the cache.  */
static bool
fonts_example (void)
{
  struct rd_message messages[] =
    {
      { .type = RD_DEFINE_FRAME, .u.define_frame = { FRAME_A, 120, 80 } },
      { .type = RD_CLEAR_AREA,
	.u.clear_area = { FRAME_A, 0, 0, 120, 80, 0xffffff } },
      { .type = RD_DEFINE_FONT, .u.define_font = { .font = 0, .size = 16,
						   .nhash = RD_HASH_SIZE } },
      { .type = RD_DEFINE_FONT, .u.define_font = { .font = 1, .size = 16 } },
      { .type = RD_DRAW_GLYPHS,
	.u.draw_glyphs = { .frame = FRAME_A, .x = 2, .y = 16,
			   .clip_width = 120, .clip_height = 40, .font = 0,
			   .nglyphs = 8 } },
      { .type = RD_DRAW_GLYPHS,
	.u.draw_glyphs = { .frame = FRAME_A, .x = 2, .y = 36,
			   .clip_width = 120, .clip_height = 60, .font = 1,
			   .nglyphs = 8 } },
      { .type = RD_DEFINE_FONT, .u.define_font = { .font = 2, .size = 16,
						   .nhash = 3 } },
      { .type = RD_DRAW_GLYPHS,
	.u.draw_glyphs = { .frame = FRAME_A, .x = 2, .y = 56,
			   .clip_width = 120, .clip_height = 80, .font = 2,
			   .nglyphs = 8 } },
      { .type = RD_DEFINE_FONT, .u.define_font = { .font = 3, .size = 16,
						   .nhash = RD_HASH_SIZE } },
      { .type = RD_DRAW_GLYPHS,
	.u.draw_glyphs = { .frame = FRAME_A, .x = 2, .y = 76,
			   .clip_width = 120, .clip_height = 80, .font = 3,
			   .nglyphs = 8 } },
    };
  enum { N = sizeof messages / sizeof *messages };
  memcpy (messages[2].u.define_font.hash, system_hash, RD_HASH_SIZE);
  memcpy (messages[8].u.define_font.hash, system_hash, RD_HASH_SIZE);
  messages[8].u.define_font.hash[0] ^= 1;
  for (int i = 0; i < 8; i++)
    messages[4].u.draw_glyphs.glyphs[i] = messages[5].u.draw_glyphs.glyphs[i]
      = messages[7].u.draw_glyphs.glyphs[i]
      = messages[9].u.draw_glyphs.glyphs[i] = 40 + i;
  uint64_t state = 2;
  struct run run;
  make_run (&run);
  char cache[160];
  snprintf (cache, sizeof cache, "%s/remote-client/font-hashes",
	    run.directory);
  bool passed = true;
  for (int i = 0; passed && i < 2; i++)
    {
      unlink (run.socket);
      unlink (run.png);
      int status = run_client (&run, messages, N, NULL, 0, &state);
      passed = (status == 0 && access (cache, F_OK) == 0
		&& same_as_renderer (run.png, messages, N, FRAME_A)
		&& !same_as_unfound (run.png, messages, N, FRAME_A));
    }
  if (!passed)
    fprintf (stderr, "test-client: example fonts: the program did not "
	     "draw with the font of the hash\n");
  else
    {
      unlink (run.socket);
      unlink (run.png);
      passed = (change_cache_hash (cache, system_font)
		&& run_client (&run, messages, N, NULL, 0, &state) == 0
		&& same_as_unfound (run.png, messages, N, FRAME_A));
      if (!passed)
	fprintf (stderr, "test-client: example fonts: the program did not "
		 "use the hash of its cache file\n");
    }
  remove_run (&run);
  return passed;
}

/* Change the first digit of the hash of the line of the file NAME in
   the cache file CACHE.  Return false if CACHE has no such line.  */
static bool
change_cache_hash (const char *cache, const char *name)
{
  gchar *text;
  if (!g_file_get_contents (cache, &text, NULL, NULL))
    return false;
  gchar **lines = g_strsplit (text, "\n", -1);
  g_free (text);
  char *end = g_strconcat (" ", name, NULL);
  bool changed = false;
  for (gchar **line = lines; *line; line++)
    if (g_str_has_suffix (*line, end))
      {
	(*line)[0] = (*line)[0] == '0' ? '1' : '0';
	changed = true;
      }
  text = g_strjoinv ("\n", lines);
  changed = changed && g_file_set_contents (cache, text, -1, NULL);
  g_free (text);
  g_free (end);
  g_strfreev (lines);
  return changed;
}

/* The colors and the place of the children example.  The child of
   10 by 10 at CHILD_X, CHILD_Y extends past the bottom of the root of
   40 by 30.  */
enum { ROOT_COLOR = 0xff0000, CHILD_COLOR = 0x0000ff, CHILD_X = 10,
       CHILD_Y = 25 };

/* The children law: FRAME_B is a visible child of FRAME_A.  If
   CHILD_FIRST, the child is defined before its root.  */
static bool
children_example (bool child_first)
{
  struct rd_message root[] =
    {
      { .type = RD_DEFINE_FRAME, .u.define_frame = { FRAME_A, 40, 30 } },
      { .type = RD_CLEAR_AREA,
	.u.clear_area = { FRAME_A, 0, 0, 40, 30, ROOT_COLOR } },
    };
  struct rd_message child[] =
    {
      { .type = RD_PLACE_FRAME,
	.u.place_frame = { .frame = FRAME_B, .parent = FRAME_A,
			   .x = CHILD_X, .y = CHILD_Y, .z = 1,
			   .visible = 1 } },
      { .type = RD_DEFINE_FRAME, .u.define_frame = { FRAME_B, 10, 10 } },
      { .type = RD_CLEAR_AREA,
	.u.clear_area = { FRAME_B, 0, 0, 10, 10, CHILD_COLOR } },
    };
  struct rd_message messages[5];
  const struct rd_message *first = child_first ? child : root;
  const struct rd_message *second = child_first ? root : child;
  int nfirst = child_first ? 3 : 2;
  memcpy (messages, first, nfirst * sizeof *messages);
  memcpy (messages + nfirst, second, (5 - nfirst) * sizeof *messages);
  uint64_t state = 3;
  struct run run;
  make_run (&run);
  bool passed = (run_client (&run, messages, 5, NULL, 0, &state) == 0
		 && composite_p (run.png));
  if (!passed)
    fprintf (stderr, "test-client: example children (child first %d): "
	     "the file is not the composite of the root\n", child_first);
  remove_run (&run);
  return passed;
}

/* Return true if the PNG file PNG has the composite of the children
   example: the size of the root, the color of the child in the area
   of the child, and the color of the root outside it.  */
static bool
composite_p (const char *png)
{
  cairo_surface_t *file = cairo_image_surface_create_from_png (png);
  bool passed = (cairo_surface_status (file) == CAIRO_STATUS_SUCCESS
		 && cairo_image_surface_get_width (file) == 40
		 && cairo_image_surface_get_height (file) == 30
		 && png_pixel (file, CHILD_X, CHILD_Y, CHILD_COLOR)
		 && png_pixel (file, CHILD_X + 9, 29, CHILD_COLOR)
		 && png_pixel (file, CHILD_X - 1, CHILD_Y, ROOT_COLOR)
		 && png_pixel (file, CHILD_X + 10, 29, ROOT_COLOR)
		 && png_pixel (file, CHILD_X, CHILD_Y - 1, ROOT_COLOR));
  cairo_surface_destroy (file);
  return passed;
}

/* Return true if the pixel X, Y of SURFACE has the color COLOR.  */
static bool
png_pixel (cairo_surface_t *surface, int x, int y, uint32_t color)
{
  uint32_t pixel;
  memcpy (&pixel, cairo_image_surface_get_data (surface)
	  + y * cairo_image_surface_get_stride (surface) + x * 4,
	  sizeof pixel);
  return (pixel & 0xffffff) == color;
}

/* The error law: a frame with a length of 0 has no type byte.  */
static bool
error_example (void)
{
  static const unsigned char bad[] = { 0, 0, 0, 0 };
  struct rd_message define = { .type = RD_DEFINE_FRAME,
			       .u.define_frame = { FRAME_A, 4, 4 } };
  uint64_t state = 1;
  struct run run;
  make_run (&run);
  int status = run_client (&run, &define, 1, bad, sizeof bad, &state);
  bool passed = status == 1 && access (run.png, F_OK) != 0;
  if (!passed)
    fprintf (stderr, "test-client: example error: the status is %d\n",
	     status);
  remove_run (&run);
  return passed;
}

/* Run the trial of SEED.  Return true if it passes.  An eighth of the
   trials define no frame, and a quarter remove frames.  A quarter of
   the trials close the connection in a frame, a quarter end with a
   frame with an error, and a quarter are long.  */
static bool
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  bool defines = random_below (&state, 8) != 0;
  bool removes = random_below (&state, 4) == 0;
  int nmessages = STEPS + (random_below (&state, 4) ? 0 : LONG_STEPS);
  static struct rd_message messages[STEPS + LONG_STEPS];
  for (int i = 0; i < nmessages; i++)
    messages[i] = (i < STEPS ? random_message (&state, defines, removes)
		   : random_long_draw (&state));
  unsigned char end[64];
  size_t nend = 0;
  int ending = random_below (&state, 4);
  if (ending == 0)
    random_cut (&state, end, &nend);
  else if (ending == 1)
    random_bad_frame (&state, end, &nend);
  struct run run;
  make_run (&run);
  int status = run_client (&run, messages, nmessages, end, nend, &state);
  uint32_t frame;
  bool written = (ending != 1 && first_frame (messages, nmessages, &frame)
		  && same_as_renderer (run.png, messages, nmessages, frame));
  bool passed = (written ? status == 0
		 : status == 1 && access (run.png, F_OK) != 0);
  if (!passed)
    fprintf (stderr, "test-client: seed %llu: %s\n",
	     (unsigned long long) seed,
	     (status == NOT_STARTED ? "./emacs-remote-client did not start"
	      : status == -1 ? "the program did not exit"
	      : "the status or the pixels are not correct"));
  remove_run (&run);
  return passed;
}

/* Set *FRAME to the frame of the first define-frame message of the N
   MESSAGES, and return true.  Return false if no message defines a
   frame.  */
static bool
first_frame (const struct rd_message *messages, int n, uint32_t *frame)
{
  for (int i = 0; i < n; i++)
    if (messages[i].type == RD_DEFINE_FRAME)
      {
	*frame = messages[i].u.define_frame.frame;
	return true;
      }
  return false;
}

/* Run the program for RUN.  Send it the N MESSAGES, and then the SIZE
   bytes EXTRA, in random chunks, and close the connection.  Return the
   exit status of the program, or -1 if it did not exit or did not
   connect.  */
static int
run_client (struct run *run, const struct rd_message *messages, int n,
	    const unsigned char *extra, size_t size, uint64_t *state)
{
  int listener = listen_at (run->socket);
  char option[120];
  snprintf (option, sizeof option, "--render-to=%s", run->png);
  pid_t pid = fork ();
  if (pid == 0)
    {
      setenv ("XDG_CACHE_HOME", run->directory, 1);
      execl ("./emacs-remote-client", "emacs-remote-client", option,
	     run->socket, (char *) NULL);
      _exit (127);
    }
  struct pollfd wait = { .fd = listener, .events = POLLIN };
  int connection = (poll (&wait, 1, CONNECT_TIMEOUT) == 1
		    ? accept (listener, NULL, NULL) : -1);
  close (listener);
  if (connection < 0)
    {
      kill (pid, SIGKILL);
      waitpid (pid, NULL, 0);
      return -1;
    }
  for (int i = 0; i < n; i++)
    {
      size_t length = rd_encoded_size (&messages[i]);
      unsigned char *bytes = malloc (length);
      rd_encode (&messages[i], bytes);
      send_in_chunks (connection, bytes, length, state);
      free (bytes);
    }
  send_in_chunks (connection, extra, size, state);
  close (connection);
  return wait_for_exit (pid);
}

/* Return the exit status of the child PID.  If it does not exit in
   EXIT_TIMEOUT milliseconds, kill it and return -1.  */
static int
wait_for_exit (pid_t pid)
{
  for (int waited = 0; waited < EXIT_TIMEOUT; waited += 10)
    {
      int status;
      if (waitpid (pid, &status, WNOHANG) == pid)
	return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
      poll (NULL, 0, 10);
    }
  kill (pid, SIGKILL);
  waitpid (pid, NULL, 0);
  return -1;
}

/* Return true if FRAME exists after the N MESSAGES in a renderer with
   the fonts of the program, and the PNG file PNG has its pixels.  The
   font source repeats the composition of client_fonts on purpose, as an
   oracle: the hash source here has only SYSTEM_FONT.  */
static bool
same_as_renderer (const char *png, const struct rd_message *messages,
		  int n, uint32_t frame)
{
  const char *list[] = { system_font };
  return same_pixels (png, messages, n, frame, rc_font_source_chain
		      (rc_hash_font_source_new (list, 1, NULL,
						rc_sha256_file, NULL),
		       rc_unhashed_font_source (rc_fallback_font_source_new ()),
		       ignore_fallback));
}

/* Return true if FRAME exists after the N MESSAGES in a renderer with
   no font file, and the PNG file PNG has its pixels.  The fallback
   font draws only the fonts with no hash or a short hash.  */
static bool
same_as_unfound (const char *png, const struct rd_message *messages,
		 int n, uint32_t frame)
{
  return same_pixels (png, messages, n, frame,
		      rc_unhashed_font_source (rc_fallback_font_source_new ()));
}

/* Return true if FRAME exists after the N MESSAGES in a renderer with
   the font source FONTS, and the PNG file PNG has its pixels.  Free
   FONTS.  The surface of a frame is opaque, so the file has no alpha
   channel to compare.  */
static bool
same_pixels (const char *png, const struct rd_message *messages, int n,
	     uint32_t frame, struct rc_font_source fonts)
{
  struct rc_renderer *renderer = rc_renderer_new (fonts);
  for (int i = 0; i < n; i++)
    rc_apply (renderer, &messages[i]);
  cairo_surface_t *expected = rc_frame_surface (renderer, frame);
  if (!expected)
    {
      rc_renderer_free (renderer);
      rc_font_source_free (fonts);
      return false;
    }
  cairo_surface_t *file = cairo_image_surface_create_from_png (png);
  bool same = cairo_surface_status (file) == CAIRO_STATUS_SUCCESS;
  int width = cairo_image_surface_get_width (expected);
  int height = cairo_image_surface_get_height (expected);
  same = (same && cairo_image_surface_get_width (file) == width
	  && cairo_image_surface_get_height (file) == height);
  cairo_surface_flush (expected);
  const unsigned char *a = cairo_image_surface_get_data (expected);
  const unsigned char *b = cairo_image_surface_get_data (file);
  for (int y = 0; same && y < height; y++)
    for (int x = 0; same && x < width; x++)
      {
	uint32_t pa, pb;
	memcpy (&pa, a + y * cairo_image_surface_get_stride (expected)
		+ x * 4, sizeof pa);
	memcpy (&pb, b + y * cairo_image_surface_get_stride (file) + x * 4,
		sizeof pb);
	same = (pa & 0xffffff) == (pb & 0xffffff);
      }
  cairo_surface_destroy (file);
  rc_renderer_free (renderer);
  rc_font_source_free (fonts);
  return same;
}

/* The function of the chain of the expected renderer.  */
static void
ignore_fallback (const struct rc_font_key *key)
{
  (void) key;
}

/* Write the SIZE BYTES to the socket FD in chunks of random sizes.  */
static void
send_in_chunks (int fd, const unsigned char *bytes, size_t size,
		uint64_t *state)
{
  size_t start = 0;
  while (start < size)
    {
      size_t chunk = (random_below (state, 4) == 0 ? size - start
		      : 1 + random_below (state, 16));
      if (chunk > size - start)
	chunk = size - start;
      ssize_t sent = send (fd, bytes + start, chunk, MSG_NOSIGNAL);
      if (sent <= 0)
	return;
      start += sent;
    }
}

/* Return a socket that listens at the file name PATH.  */
static int
listen_at (const char *path)
{
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  snprintf (address.sun_path, sizeof address.sun_path, "%s", path);
  if (fd < 0 || bind (fd, (struct sockaddr *) &address, sizeof address) != 0
      || listen (fd, 1) != 0)
    {
      perror ("test-client: listen");
      exit (2);
    }
  return fd;
}

/* Make a new directory for RUN, and set the names of its files.  */
static void
make_run (struct run *run)
{
  snprintf (run->directory, sizeof run->directory,
	    "/tmp/test-client-XXXXXX");
  if (!mkdtemp (run->directory))
    {
      perror ("test-client: mkdtemp");
      exit (2);
    }
  snprintf (run->socket, sizeof run->socket, "%s/socket", run->directory);
  snprintf (run->png, sizeof run->png, "%s/frame.png", run->directory);
}

/* Remove the files and the directories of RUN.  */
static void
remove_run (const struct run *run)
{
  char cache[160];
  unlink (run->socket);
  unlink (run->png);
  snprintf (cache, sizeof cache, "%s/remote-client/font-hashes",
	    run->directory);
  unlink (cache);
  snprintf (cache, sizeof cache, "%s/remote-client", run->directory);
  rmdir (cache);
  rmdir (run->directory);
}

/* Return a random message.  If DEFINES, some messages define the
   frames FRAME_A and FRAME_B.  If REMOVES, some messages remove them.
   The other messages define fonts, and draw in the two frames.  */
static struct rd_message
random_message (uint64_t *state, bool defines, bool removes)
{
  uint32_t frame = random_below (state, 2) ? FRAME_A : FRAME_B;
  int32_t x = (int32_t) random_below (state, 70) - 5;
  int32_t y = (int32_t) random_below (state, 50) - 5;
  uint32_t color = random_next (state);
  switch (random_below (state, 9))
    {
    case 0:
      if (!defines)
	return (struct rd_message) { .type = RD_HELLO };
      return (struct rd_message)
	{ .type = RD_DEFINE_FRAME,
	  .u.define_frame = { frame, 1 + random_below (state, 60),
			      1 + random_below (state, 40) } };
    case 7:
      if (!removes || random_below (state, 4))
	return (struct rd_message) { .type = RD_HELLO };
      return (struct rd_message) { .type = RD_DELETE_FRAME,
				   .u.delete_frame = { frame } };
    case 8:
      if (!removes || random_below (state, 4))
	return (struct rd_message) { .type = RD_HELLO };
      return (struct rd_message)
	{ .type = RD_DEFINE_FRAME,
	  .u.define_frame = { frame, RC_MAX_FRAME_SIZE + 1, 10 } };
    case 1:
      {
	struct rd_message message =
	  { .type = RD_DEFINE_FONT,
	    .u.define_font = { .font = random_below (state, 3),
			       .size = 6 + random_below (state, 24) } };
	if (random_below (state, 2))
	  {
	    message.u.define_font.nhash = RD_HASH_SIZE;
	    memcpy (message.u.define_font.hash, system_hash, RD_HASH_SIZE);
	  }
	return message;
      }
    case 2:
      return (struct rd_message)
	{ .type = RD_CLEAR_AREA,
	  .u.clear_area = { frame, x, y, random_below (state, 40),
			    random_below (state, 30), color } };
    case 3:
      return (struct rd_message)
	{ .type = RD_COPY_AREA,
	  .u.copy_area = { frame, x, y, random_below (state, 40),
			   random_below (state, 30),
			   (int32_t) random_below (state, 60) - 5,
			   (int32_t) random_below (state, 40) - 5 } };
    case 4:
      return (struct rd_message)
	{ .type = RD_DRAW_CURSOR,
	  .u.draw_cursor = { frame, x, y, random_below (state, 10),
			     random_below (state, 20),
			     random_below (state, RD_CURSOR_HBAR + 1),
			     color } };
    case 5:
      {
	struct rd_message message = { .type = RD_DRAW_FRINGE };
	struct rd_draw_fringe *m = &message.u.draw_fringe;
	*m = (struct rd_draw_fringe)
	  { .frame = frame, .x = x, .y = y, .width = random_below (state, 9),
	    .overlay = random_below (state, 2), .foreground = color,
	    .background = random_next (state),
	    .nrows = random_below (state, 20) };
	for (int i = 0; i < m->nrows; i++)
	  m->rows[i] = random_next (state);
	return message;
      }
    default:
      {
	struct rd_message message = { .type = RD_DRAW_GLYPHS };
	struct rd_draw_glyphs *m = &message.u.draw_glyphs;
	*m = (struct rd_draw_glyphs)
	  { .frame = frame, .x = x, .y = y, .clip_x = 0, .clip_y = 0,
	    .clip_width = random_below (state, 70),
	    .clip_height = random_below (state, 50), .color = color,
	    .font = random_below (state, 3),
	    .nglyphs = (random_below (state, 8) == 0
			? random_below (state, RD_MAX_GLYPHS + 1)
			: random_below (state, 8)) };
	for (int i = 0; i < m->nglyphs; i++)
	  m->glyphs[i] = random_below (state, 100);
	return message;
      }
    }
}

/* Return a draw-glyphs message of RD_MAX_GLYPHS random glyphs in the
   frame FRAME_A.  */
static struct rd_message
random_long_draw (uint64_t *state)
{
  struct rd_message message = { .type = RD_DRAW_GLYPHS };
  struct rd_draw_glyphs *m = &message.u.draw_glyphs;
  *m = (struct rd_draw_glyphs)
    { .frame = FRAME_A, .x = (int32_t) random_below (state, 70) - 5,
      .y = random_below (state, 50), .clip_width = 60, .clip_height = 40,
      .color = random_next (state), .font = random_below (state, 3),
      .nglyphs = RD_MAX_GLYPHS };
  for (int i = 0; i < m->nglyphs; i++)
    m->glyphs[i] = random_below (state, 100);
  return message;
}

/* Set the *SIZE bytes BAD to a frame with an error: a frame with no
   type byte, a frame longer than RD_MAX_LENGTH, or a frame of a known
   type that is too short for the type.  BAD has 64 bytes.  */
static void
random_bad_frame (uint64_t *state, unsigned char *bad, size_t *size)
{
  static const unsigned char frames[][5] =
    {
      { 0, 0, 0, 0 },
      { 1, 0, 0, 1, RD_HELLO },
      { 0, 0, 0, 1, RD_DEFINE_FRAME },
    };
  int i = random_below (state, 3);
  *size = i == 0 ? 4 : 5;
  memcpy (bad, frames[i], *size);
}

/* Set the *SIZE bytes CUT to the start of the encoding of a random
   message: at least one byte, and not all bytes.  CUT has 64 bytes.  */
static void
random_cut (uint64_t *state, unsigned char *cut, size_t *size)
{
  struct rd_message message =
    { .type = RD_CLEAR_AREA,
      .u.clear_area = { FRAME_A, 0, 0, 10, 10, random_next (state) } };
  unsigned char bytes[64];
  size_t length = rd_encoded_size (&message);
  rd_encode (&message, bytes);
  *size = 1 + random_below (state, length - 1);
  memcpy (cut, bytes, *size);
}

/* Return a random number from 0 to LIMIT - 1.  */
static uint64_t
random_below (uint64_t *state, uint64_t limit)
{
  return random_next (state) % limit;
}

/* Return the next number of the generator splitmix64.  */
static uint64_t
random_next (uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}
