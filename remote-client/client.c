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

/* Usage: remote-client [--render-to=FILE.png | --timeout=MS] SOCKET

   The program connects to the Unix-domain socket SOCKET of a session,
   and shows each frame of the session in a window.  It stops when the
   user closes a window, or when the connection ends and the program
   has no window.

   When the connection ends, or no bytes come from the server for MS
   milliseconds, the program keeps its windows and connects to SOCKET
   again.  The title of each window shows the loss until the server
   sends bytes on the new connection.  The program drops the input of
   the user while it has no connection.  The default of MS is
   RC_LINK_DEFAULT_TIMEOUT.  A slow network can need a larger value.

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
#include "link.h"
#include "backoff.h"
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

/* A connection of the mode with windows: its file descriptor FD, the
   source of the GTK main loop that reads FD, the reader of its
   messages, and its liveness.  HEARD is true after the first bytes
   from the server.  BROKEN is true after a failed write.  A write
   can come from a function that the reader calls, so the write does
   not close the connection.  read_input or tick_link closes it
   later.  */
struct connection
{
  int fd;
  guint watch;
  struct rc_reader *reader;
  struct rc_link link;
  bool heard;
  bool broken;
};

/* The state of the mode with windows.  PATH is the socket of the
   session, and TIMEOUT is the timeout of a connection.  CONNECTION is
   NULL while the program waits to connect again.  TICK is the timer of
   the liveness, and RETRY is the timer of the next connect, or 0.  */
struct window_mode
{
  struct rc_renderer *renderer;
  struct rc_view *view;
  const char *path;
  int64_t timeout;
  struct connection *connection;
  struct rc_backoff backoff;
  guint tick;
  guint retry;
  /* True if the last connection had an error.  */
  bool failed;
};

static struct rc_font_source client_fonts (void);
static char *cache_file_name (void);
static void warn_fallback (const uint8_t *, int, uint32_t);
static int render_to_file (struct rc_renderer *, int, const char *);
static void record_first_frame (void *, const struct rd_message *);
static int show_frames (struct rc_renderer *, int, const char *, int64_t);
static void open_connection (struct window_mode *, int);
static void lose_connection (struct window_mode *, bool);
static void close_connection (struct window_mode *);
static void schedule_retry (struct window_mode *);
static gboolean retry_connect (gpointer);
static gboolean tick_link (gpointer);
static void network_changed (GNetworkMonitor *, gboolean, gpointer);
static void show_message (void *, const struct rd_message *);
static gboolean read_input (gint, GIOCondition, gpointer);
static void note_input (struct window_mode *);
static void stop_main_loop (void *);
static void send_message (void *, const struct rd_message *);
static void send_on (struct connection *, const struct rd_message *,
		     int64_t);
static bool write_all (int, const unsigned char *, size_t, int64_t);
static enum read_result read_chunk (struct rc_reader *, int);
static int connect_to (const char *, bool);
static int64_t now_ms (void);
static void usage (void);

int
main (int argc, char **argv)
{
  const char *png = NULL;
  int64_t timeout = RC_LINK_DEFAULT_TIMEOUT;
  int arg = 1;
  if (arg < argc && strncmp (argv[arg], "--render-to=", 12) == 0)
    png = argv[arg++] + 12;
  else if (arg < argc && strncmp (argv[arg], "--timeout=", 10) == 0)
    {
      char *end;
      timeout = strtoll (argv[arg++] + 10, &end, 10);
      if (*end || timeout < RC_LINK_PINGS_PER_TIMEOUT)
	usage ();
    }
  if (arg != argc - 1)
    usage ();
  int fd = connect_to (argv[arg], true);
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
      status = show_frames (renderer, fd, argv[arg], timeout);
    }
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
  close (fd);
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
   the connection FD to the socket PATH to RENDERER.  Connect to PATH
   again after the loss of a connection with TIMEOUT.  Stop when the
   user closes a window, or when a connection ends and no window is
   left.  Return the exit status.  */
static int
show_frames (struct rc_renderer *renderer, int fd, const char *path,
	     int64_t timeout)
{
  struct window_mode mode = { .renderer = renderer, .path = path,
			      .timeout = timeout,
			      .backoff = rc_backoff_new () };
  mode.view = rc_view_new (renderer, (struct rc_view_functions)
			   { stop_main_loop, send_message, &mode });
  open_connection (&mode, fd);
  mode.tick = g_timeout_add (rc_link_tick_interval (timeout), tick_link,
			     &mode);
  GNetworkMonitor *monitor = g_network_monitor_get_default ();
  gulong handler = g_signal_connect (monitor, "network-changed",
				     G_CALLBACK (network_changed), &mode);
  gtk_main ();
  g_signal_handler_disconnect (monitor, handler);
  g_source_remove (mode.tick);
  if (mode.retry)
    g_source_remove (mode.retry);
  close_connection (&mode);
  rc_view_free (mode.view);
  return mode.failed ? 1 : 0;
}

/* Make the open file descriptor FD the connection of MODE.  */
static void
open_connection (struct window_mode *mode, int fd)
{
  struct connection *connection = g_new (struct connection, 1);
  *connection = (struct connection)
    { .fd = fd, .reader = rc_reader_new (show_message, mode),
      .link = rc_link_new (now_ms (), mode->timeout) };
  fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
  connection->watch = g_unix_fd_add (fd, G_IO_IN | G_IO_HUP | G_IO_ERR,
				     read_input, mode);
  mode->connection = connection;
}

/* Close the connection of MODE, which ended or is lost.  FAILED is
   true if the connection had an error.  With no window, stop the main
   loop.  Else show the loss, and connect again later.  */
static void
lose_connection (struct window_mode *mode, bool failed)
{
  close_connection (mode);
  mode->failed = failed;
  if (rc_view_window_count (mode->view) == 0)
    {
      gtk_main_quit ();
      return;
    }
  fprintf (stderr, "remote-client: the connection is lost; "
	   "the program connects again\n");
  rc_view_set_connected (mode->view, false);
  schedule_retry (mode);
}

/* Close the connection of MODE, if it has one.  */
static void
close_connection (struct window_mode *mode)
{
  struct connection *connection = mode->connection;
  if (!connection)
    return;
  if (connection->watch)
    g_source_remove (connection->watch);
  close (connection->fd);
  rc_reader_free (connection->reader);
  g_free (connection);
  mode->connection = NULL;
}

/* Connect MODE again after the next wait of its backoff.  */
static void
schedule_retry (struct window_mode *mode)
{
  mode->retry = g_timeout_add (rc_backoff_next (&mode->backoff),
			       retry_connect, mode);
}

/* The timer of the next connect.  DATA is the struct window_mode.
   Connect to the socket, or wait longer after a failure.  */
static gboolean
retry_connect (gpointer data)
{
  struct window_mode *mode = data;
  mode->retry = 0;
  int fd = connect_to (mode->path, false);
  if (fd < 0)
    schedule_retry (mode);
  else
    open_connection (mode, fd);
  return G_SOURCE_REMOVE;
}

/* The timer of the liveness.  DATA is the struct window_mode.  Send a
   ping, or lose the connection, as its link decides.  */
static gboolean
tick_link (gpointer data)
{
  struct window_mode *mode = data;
  struct connection *connection = mode->connection;
  if (!connection)
    return G_SOURCE_CONTINUE;
  if (connection->broken)
    {
      lose_connection (mode, true);
      return G_SOURCE_CONTINUE;
    }
  uint32_t serial;
  switch (rc_link_tick (&connection->link, now_ms (), &serial))
    {
    case RC_LINK_PING:
      send_on (connection, &(struct rd_message)
	       { .type = RD_PING, .u.ping = { .serial = serial } },
	       mode->timeout);
      break;
    case RC_LINK_LOST:
      lose_connection (mode, false);
      break;
    case RC_LINK_WAIT:
      break;
    }
  return G_SOURCE_CONTINUE;
}

/* The signal network-changed of the network monitor.  DATA is the
   struct window_mode.  Make the link of the connection send a ping
   at once.  */
static void
network_changed (GNetworkMonitor *monitor, gboolean available,
		 gpointer data)
{
  (void) monitor;
  (void) available;
  struct window_mode *mode = data;
  if (mode->connection)
    rc_link_note_network_change (&mode->connection->link);
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
   bytes are ready.  At the end of the connection, at an error, or
   after a failed write, lose the connection.  */
static gboolean
read_input (gint fd, GIOCondition condition, gpointer data)
{
  (void) condition;
  struct window_mode *mode = data;
  enum read_result result = read_chunk (mode->connection->reader, fd);
  if (result == READ_MORE)
    note_input (mode);
  bool broken = mode->connection->broken;
  if (!broken && (result == READ_MORE || result == READ_WAIT))
    return G_SOURCE_CONTINUE;
  mode->connection->watch = 0;
  lose_connection (mode, broken || result == READ_ERROR);
  return G_SOURCE_REMOVE;
}

/* Record that bytes came on the connection of MODE.  After the first
   bytes of a connection, the connection works: show that, and start
   the backoff again.  */
static void
note_input (struct window_mode *mode)
{
  struct connection *connection = mode->connection;
  rc_link_note_input (&connection->link, now_ms ());
  if (connection->heard)
    return;
  connection->heard = true;
  rc_backoff_reset (&mode->backoff);
  rc_view_set_connected (mode->view, true);
}

/* The function of the view when the user closes a window.  */
static void
stop_main_loop (void *data)
{
  (void) data;
  gtk_main_quit ();
}

/* The function of the view for each message of the input of the user.
   DATA is the struct window_mode.  Send MESSAGE to the server.  With
   no connection, drop MESSAGE.  */
static void
send_message (void *data, const struct rd_message *message)
{
  struct window_mode *mode = data;
  if (mode->connection)
    send_on (mode->connection, message, mode->timeout);
}

/* Send MESSAGE on CONNECTION, with a wait of at most LIMIT
   milliseconds.  After a failed write, report the error, and mark
   CONNECTION as broken.  Do nothing on a broken connection.  */
static void
send_on (struct connection *connection, const struct rd_message *message,
	 int64_t limit)
{
  if (connection->broken)
    return;
  size_t size = rd_encoded_size (message);
  unsigned char *bytes = g_malloc (size);
  rd_encode (message, bytes);
  if (!write_all (connection->fd, bytes, size, limit))
    {
      fprintf (stderr, "remote-client: a write to the server failed\n");
      connection->broken = true;
    }
  g_free (bytes);
}

/* Write the SIZE BYTES to the connection FD, which does not block.
   When the connection cannot take more bytes now, wait until it can,
   but at most LIMIT milliseconds in all.  Return false on an error or
   after LIMIT.  An input message is small, so the wait is short on a
   live connection.  */
static bool
write_all (int fd, const unsigned char *bytes, size_t size, int64_t limit)
{
  int64_t end = now_ms () + limit;
  while (size > 0)
    {
      ssize_t n = send (fd, bytes, size, MSG_NOSIGNAL);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
	{
	  int64_t left = end - now_ms ();
	  struct pollfd ready = { .fd = fd, .events = POLLOUT };
	  if (left <= 0 || poll (&ready, 1, left) == 0)
	    return false;
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
   error.  If REPORT, write the error to the standard error output.  */
static int
connect_to (const char *path, bool report)
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
      if (report)
	fprintf (stderr, "remote-client: %s: %s\n", path, strerror (errno));
      if (fd >= 0)
	close (fd);
      return -1;
    }
  return fd;
}

/* Return the time of a monotonic clock in milliseconds.  */
static int64_t
now_ms (void)
{
  return g_get_monotonic_time () / 1000;
}

static void
usage (void)
{
  fprintf (stderr, "Usage: remote-client [--render-to=FILE.png | "
	   "--timeout=MS] SOCKET\n");
  exit (1);
}
