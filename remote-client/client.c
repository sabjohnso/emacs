/* The program remote-client: the client of a remote display session.

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

/* Usage: remote-client [--render-to=FILE.png] SOCKET

   The program connects to the Unix-domain socket SOCKET of a session,
   and shows each frame of the session in a window.  It stops when
   the server closes the connection, or when the user closes a window.

   With --render-to=FILE.png, the program opens no window.  It reads
   the messages until the server closes the connection.  Then it
   writes the surface of the first frame that the server defined to
   FILE.png, and stops.  This mode needs no display, so a test can use
   it.

   The exit status is 0 on success, and 1 on an error.  */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <glib-unix.h>
#include <gtk/gtk.h>

#include "fonts.h"
#include "hashcache.h"
#include "hashfonts.h"
#include "reader.h"
#include "render.h"
#include "view.h"

/* The size of each read from the socket.  */
enum { READ_SIZE = 65536 };

/* The results of one read from the connection.  */
enum read_result
  {
    /* The reader got the bytes.  */
    READ_MORE,
    /* No bytes are ready now.  */
    READ_WAIT,
    /* The server closed the connection.  */
    READ_END,
    /* The read or the stream has an error.  */
    READ_ERROR,
  };

/* The state of the mode --render-to.  */
struct png_mode
{
  struct rc_renderer *renderer;
  /* The frame of the first define-frame message, if HAVE_FIRST.  */
  uint32_t first_frame;
  bool have_first;
};

/* The state of the mode with windows.  FD is the connection.  */
struct window_mode
{
  struct rc_renderer *renderer;
  struct rc_reader *reader;
  struct rc_view *view;
  int fd;
  /* True if the connection had an error.  */
  bool failed;
};

static struct rc_font_source client_fonts (void);
static char *cache_file_name (void);
static void warn_fallback (const uint8_t *, int, uint32_t);
static int render_to_file (struct rc_renderer *, int, const char *);
static void record_first_frame (void *, const struct rd_message *);
static int show_frames (struct rc_renderer *, int);
static void show_message (void *, const struct rd_message *);
static gboolean read_input (gint, GIOCondition, gpointer);
static void stop_main_loop (void *);
static void send_message (void *, const struct rd_message *);
static bool write_all (int, const unsigned char *, size_t);
static enum read_result read_chunk (struct rc_reader *, int);
static int connect_to (const char *);
static void usage (void);

int
main (int argc, char **argv)
{
  const char *png = NULL;
  int arg = 1;
  if (arg < argc && strncmp (argv[arg], "--render-to=", 12) == 0)
    png = argv[arg++] + 12;
  if (arg != argc - 1)
    usage ();
  int fd = connect_to (argv[arg]);
  if (fd < 0)
    return 1;
  struct rc_font_source fonts = client_fonts ();
  struct rc_renderer *renderer = rc_renderer_new (fonts);
  int status;
  if (png)
    status = render_to_file (renderer, fd, png);
  else
    {
      gtk_init (&argc, &argv);
      status = show_frames (renderer, fd);
    }
  close (fd);
  rc_renderer_free (renderer);
  rc_font_source_free (fonts);
  return status;
}

/* Return the font source of the client: the font file with the hash
   of a font, from the font files of fontconfig, or else the fallback
   font.  */
static struct rc_font_source
client_fonts (void)
{
  char **names = rc_font_file_names ();
  char *cache = cache_file_name ();
  struct rc_font_source hashed
    = rc_hash_font_source_new ((const char *const *) names,
			       g_strv_length (names), cache, rc_sha256_file,
			       NULL);
  g_strfreev (names);
  g_free (cache);
  return rc_font_source_chain (hashed, rc_fallback_font_source_new (),
			       warn_fallback);
}

/* Return the name of the cache file of font hashes, in a new
   string.  */
static char *
cache_file_name (void)
{
  return g_build_filename (g_get_user_cache_dir (), "remote-client",
			   "font-hashes", NULL);
}

/* The function of the font chain when no font file has the NHASH bytes
   HASH.  A hash with fewer than RD_HASH_SIZE bytes means that the
   server could not read its font file.  */
static void
warn_fallback (const uint8_t *hash, int nhash, uint32_t index)
{
  if (nhash < RD_HASH_SIZE)
    {
      fprintf (stderr, "remote-client: the server could not read a font "
	       "file, so the fallback font draws its glyphs\n");
      return;
    }
  char hex[2 * RD_HASH_SIZE + 1];
  rc_hash_to_hex (hash, hex);
  fprintf (stderr, "remote-client: no font file has the hash %s (face "
	   "%lu), so the fallback font draws its glyphs\n", hex,
	   (unsigned long) index);
}

/* Apply all messages from the connection FD to RENDERER, and write
   the first frame to the file PNG.  Return the exit status.  */
static int
render_to_file (struct rc_renderer *renderer, int fd, const char *png)
{
  struct png_mode mode = { .renderer = renderer };
  struct rc_reader *reader = rc_reader_new (record_first_frame, &mode);
  enum read_result result;
  do
    result = read_chunk (reader, fd);
  while (result == READ_MORE);
  rc_reader_free (reader);
  if (result == READ_ERROR)
    return 1;
  cairo_surface_t *surface
    = (mode.have_first ? rc_frame_surface (renderer, mode.first_frame)
       : NULL);
  if (!surface)
    {
      fprintf (stderr, "remote-client: the first frame does not exist\n");
      return 1;
    }
  cairo_status_t status = cairo_surface_write_to_png (surface, png);
  if (status != CAIRO_STATUS_SUCCESS)
    {
      fprintf (stderr, "remote-client: %s: %s\n", png,
	       cairo_status_to_string (status));
      return 1;
    }
  return 0;
}

/* The function of the reader of the mode --render-to.  DATA is the
   struct png_mode.  Apply MESSAGE, and record the first frame.  */
static void
record_first_frame (void *data, const struct rd_message *message)
{
  struct png_mode *mode = data;
  rc_apply (mode->renderer, message);
  if (message->type == RD_DEFINE_FRAME && !mode->have_first)
    {
      mode->first_frame = message->u.define_frame.frame;
      mode->have_first = true;
    }
}

/* Show the frames of RENDERER in windows, and apply the messages from
   the connection FD to RENDERER, until the server closes the
   connection or the user closes a window.  Return the exit status.  */
static int
show_frames (struct rc_renderer *renderer, int fd)
{
  struct window_mode mode = { .renderer = renderer, .fd = fd };
  mode.reader = rc_reader_new (show_message, &mode);
  mode.view = rc_view_new (renderer, (struct rc_view_functions)
			   { stop_main_loop, send_message, &mode });
  fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
  guint source = g_unix_fd_add (fd, G_IO_IN | G_IO_HUP | G_IO_ERR,
				read_input, &mode);
  gtk_main ();
  g_source_remove (source);
  rc_view_free (mode.view);
  rc_reader_free (mode.reader);
  return mode.failed ? 1 : 0;
}

/* The function of the reader of the mode with windows.  DATA is the
   struct window_mode.  Apply MESSAGE, and show its change.  */
static void
show_message (void *data, const struct rd_message *message)
{
  struct window_mode *mode = data;
  rc_view_update (mode->view, rc_apply (mode->renderer, message));
}

/* The function of the GTK main loop for the connection FD.  DATA is
   the struct window_mode.  Read one chunk, so that the main loop can
   draw between chunks.  The main loop calls this function again while
   bytes are ready.  Stop the main loop at the end of the connection,
   or at an error.  */
static gboolean
read_input (gint fd, GIOCondition condition, gpointer data)
{
  (void) condition;
  struct window_mode *mode = data;
  enum read_result result = read_chunk (mode->reader, fd);
  if (result == READ_MORE || result == READ_WAIT)
    return G_SOURCE_CONTINUE;
  mode->failed = result == READ_ERROR;
  gtk_main_quit ();
  return G_SOURCE_CONTINUE;
}

/* The function of the view when the user closes a window.  */
static void
stop_main_loop (void *data)
{
  (void) data;
  gtk_main_quit ();
}

/* The function of the view for each message of the input of the user.
   DATA is the struct window_mode.  Send MESSAGE to the server.  After
   a failed write, stop the main loop.  */
static void
send_message (void *data, const struct rd_message *message)
{
  struct window_mode *mode = data;
  size_t size = rd_encoded_size (message);
  unsigned char *bytes = g_malloc (size);
  rd_encode (message, bytes);
  if (!write_all (mode->fd, bytes, size))
    {
      perror ("remote-client: write");
      mode->failed = true;
      gtk_main_quit ();
    }
  g_free (bytes);
}

/* Write the SIZE BYTES to the connection FD, which does not block.
   When the connection cannot take more bytes now, wait until it can.
   Return false on an error.  An input message is small, so the wait
   is short.  */
static bool
write_all (int fd, const unsigned char *bytes, size_t size)
{
  while (size > 0)
    {
      ssize_t n = send (fd, bytes, size, MSG_NOSIGNAL);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
	{
	  struct pollfd ready = { .fd = fd, .events = POLLOUT };
	  poll (&ready, 1, -1);
	  continue;
	}
      if (n < 0 && errno == EINTR)
	continue;
      if (n < 0)
	return false;
      bytes += n;
      size -= n;
    }
  return true;
}

/* Read one chunk from the connection FD, and give it to READER.
   Write an error to the standard error output.  */
static enum read_result
read_chunk (struct rc_reader *reader, int fd)
{
  unsigned char buffer[READ_SIZE];
  ssize_t n = read (fd, buffer, sizeof buffer);
  if (n == 0)
    return READ_END;
  if (n < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
	return READ_WAIT;
      if (errno == EINTR)
	return READ_MORE;
      perror ("remote-client: read");
      return READ_ERROR;
    }
  if (!rc_reader_feed (reader, buffer, n))
    {
      fprintf (stderr, "remote-client: %s\n", rc_reader_error (reader));
      return READ_ERROR;
    }
  return READ_MORE;
}

/* Return a connection to the Unix-domain socket PATH, or -1 on an
   error.  */
static int
connect_to (const char *path)
{
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  if (strlen (path) >= sizeof address.sun_path)
    {
      fprintf (stderr, "remote-client: %s: the name is too long\n", path);
      return -1;
    }
  strcpy (address.sun_path, path);
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0
      || connect (fd, (struct sockaddr *) &address, sizeof address) != 0)
    {
      fprintf (stderr, "remote-client: %s: %s\n", path, strerror (errno));
      if (fd >= 0)
	close (fd);
      return -1;
    }
  return fd;
}

static void
usage (void)
{
  fprintf (stderr, "Usage: remote-client [--render-to=FILE.png] SOCKET\n");
  exit (1);
}
