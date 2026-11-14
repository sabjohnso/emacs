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
	  remote-client [--timeout=MS] [--ssh-command=PROGRAM] --ssh HOST
	  remote-client --relay SOCKET

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

   With --ssh HOST, the program starts a new session on HOST through
   SSH, and connects to it through the relay of relay.h and an SSH
   tunnel: see bootstrap.h and tunnel.h.  PROGRAM is the SSH program,
   "ssh" by default.  The Emacs server on HOST must run.

   With --relay, the program is the relay of relay.h for the session
   socket SOCKET.  It opens no window.

   The exit status is 0 on success, and 1 on an error.  */

#include <config.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <glib-unix.h>
#include <gtk/gtk.h>

#include "bootstrap.h"
#include "connector.h"
#include "fonts.h"
#include "hashcache.h"
#include "hashfonts.h"
#include "link.h"
#include "backoff.h"
#include "reader.h"
#include "relay.h"
#include "render.h"
#include "stream.h"
#include "tunnel.h"
#include "unixsocket.h"
#include "view.h"

/* The size of each read from the stream.  After a read of this size,
   the stream holds no buffered bytes, so read_input needs no check of
   rc_stream_buffered_p.  */
enum { READ_SIZE = 65536 };
static_assert ((int) READ_SIZE >= (int) RC_STREAM_RECORD_SIZE,
	       "a read must take all the bytes of a stream");

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

/* The options of the command line.  PNG is the file of --render-to,
   or NULL.  HOST is the host of --ssh, or NULL; then SOCKET is the
   session socket.  SSH is the program of --ssh-command.  */
struct options
{
  const char *png;
  int64_t timeout;
  const char *ssh;
  const char *host;
  const char *socket;
};

/* The state of the mode --render-to.  */
struct png_mode
{
  struct rc_renderer *renderer;
  /* The frame of the first define-frame message, if HAVE_FIRST.  */
  uint32_t first_frame;
  bool have_first;
};

/* A connection of the mode with windows: its stream, the source of
   the GTK main loop that reads the socket of the stream, the reader of its
   messages, and its liveness.  HEARD is true after the first bytes
   from the server.  BROKEN is true after a failed write.  A write
   can come from a function that the reader calls, so the write does
   not close the connection.  read_input or tick_link closes it
   later.  */
struct connection
{
  struct rc_stream *stream;
  guint watch;
  struct rc_reader *reader;
  struct rc_link link;
  bool heard;
  bool broken;
};

/* The state of the mode with windows.  CONNECTOR connects to the
   session again, and TIMEOUT is the timeout of a connection.  CONNECTION is
   NULL while the program waits to connect again.  TICK is the timer of
   the liveness, and RETRY is the timer of the next connect, or 0.  */
struct window_mode
{
  struct rc_renderer *renderer;
  struct rc_view *view;
  struct rc_connector connector;
  int64_t timeout;
  struct connection *connection;
  struct rc_backoff backoff;
  guint tick;
  guint retry;
  /* True if the last connection had an error.  */
  bool failed;
};

static struct options parse_options (int, char **);
static struct rc_stream *open_session (const struct options *,
				       struct rc_connector *,
				       struct rc_tunnel **);
static struct rc_stream *open_ssh_session (const struct options *,
					   struct rc_connector *,
					   struct rc_tunnel **);
static struct rc_font_source client_fonts (void);
static char *cache_file_name (void);
static void warn_fallback (const uint8_t *, int, uint32_t);
static int render_to_file (struct rc_renderer *, struct rc_stream *,
			   const char *);
static void record_first_frame (void *, const struct rd_message *);
static int show_frames (struct rc_renderer *, struct rc_stream *,
			struct rc_connector, int64_t);
static struct rc_stream *connect_socket (void *);
static void open_connection (struct window_mode *, struct rc_stream *);
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
static bool write_all (struct rc_stream *, const unsigned char *, size_t,
		       int64_t);
static enum read_result read_chunk (struct rc_reader *, struct rc_stream *);
static int64_t now_ms (void);
static void usage (void);

int
main (int argc, char **argv)
{
  if (argc > 1 && strcmp (argv[1], "--relay") == 0)
    {
      if (argc != 3)
	usage ();
      return rc_relay_run (argv[2]);
    }
  struct options options = parse_options (argc, argv);
  struct rc_connector connector;
  struct rc_tunnel *tunnel = NULL;
  struct rc_stream *stream = open_session (&options, &connector, &tunnel);
  if (!stream)
    {
      rc_tunnel_free (tunnel);
      return 1;
    }
  struct rc_font_source fonts = client_fonts ();
  struct rc_renderer *renderer = rc_renderer_new (fonts);
  int status;
  if (options.png)
    status = render_to_file (renderer, stream, options.png);
  else
    {
      gtk_init (&argc, &argv);
      status = show_frames (renderer, stream, connector, options.timeout);
    }
  rc_renderer_free (renderer);
  rc_font_source_free (fonts);
  rc_tunnel_free (tunnel);
  return status;
}

/* Return the options of the command line ARGV with ARGC arguments.
   Exit with a message if they are not valid.  */
static struct options
parse_options (int argc, char **argv)
{
  struct options options = { .timeout = RC_LINK_DEFAULT_TIMEOUT,
			     .ssh = "ssh" };
  int arg = 1;
  for (; arg < argc && strncmp (argv[arg], "--", 2) == 0; arg++)
    if (strncmp (argv[arg], "--render-to=", 12) == 0)
      options.png = argv[arg] + 12;
    else if (strncmp (argv[arg], "--timeout=", 10) == 0)
      {
	char *end;
	options.timeout = strtoll (argv[arg] + 10, &end, 10);
	if (*end || options.timeout < RC_LINK_PINGS_PER_TIMEOUT)
	  usage ();
      }
    else if (strncmp (argv[arg], "--ssh-command=", 14) == 0)
      options.ssh = argv[arg] + 14;
    else if (strcmp (argv[arg], "--ssh") == 0 && arg + 1 < argc)
      options.host = argv[++arg];
    else
      usage ();
  if (options.host ? arg != argc || options.png : arg != argc - 1)
    usage ();
  options.socket = options.host ? NULL : argv[arg];
  return options;
}

/* Connect to the session of OPTIONS, and return the stream, or NULL
   with a message.  Put the connector for later connections in
   *CONNECTOR.  With --ssh, put the tunnel in *TUNNEL.  */
static struct rc_stream *
open_session (const struct options *options, struct rc_connector *connector,
	      struct rc_tunnel **tunnel)
{
  if (options->host)
    return open_ssh_session (options, connector, tunnel);
  *connector = (struct rc_connector) { connect_socket,
				       (void *) options->socket };
  int fd = rc_connect_unix (options->socket, true);
  return fd < 0 ? NULL : rc_stream_socket (fd);
}

/* Start a session on the host of OPTIONS through SSH, and return the
   first stream through a new tunnel, or NULL with a message.  Put
   the connector of the tunnel in *CONNECTOR, and the tunnel in
   *TUNNEL.  */
static struct rc_stream *
open_ssh_session (const struct options *options,
		  struct rc_connector *connector, struct rc_tunnel **tunnel)
{
  struct rc_relay_line line;
  if (!rc_bootstrap (options->ssh, options->host, &line))
    return NULL;
  const char *base = getenv ("XDG_RUNTIME_DIR");
  *tunnel = rc_tunnel_new (options->ssh, options->host, &line,
			   base && *base ? base : "/tmp");
  if (!*tunnel)
    return NULL;
  *connector = (struct rc_connector) { rc_tunnel_connect, *tunnel };
  return rc_tunnel_first_connect (*tunnel);
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

/* Apply all messages from STREAM to RENDERER, and write the first
   frame to the file PNG.  Return the exit status.  The socket of
   STREAM blocks, so a wait comes only from a signal.  */
static int
render_to_file (struct rc_renderer *renderer, struct rc_stream *stream,
		const char *png)
{
  struct png_mode mode = { .renderer = renderer };
  struct rc_reader *reader = rc_reader_new (record_first_frame, &mode);
  enum read_result result;
  do
    result = read_chunk (reader, stream);
  while (result == READ_MORE || result == READ_WAIT);
  rc_reader_free (reader);
  rc_stream_close (stream);
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
   STREAM to RENDERER.  Connect to PATH
   again after the loss of a connection with TIMEOUT.  Stop when the
   user closes a window, or when a connection ends and no window is
   left.  Return the exit status.  */
static int
show_frames (struct rc_renderer *renderer, struct rc_stream *stream,
	     struct rc_connector connector, int64_t timeout)
{
  struct window_mode mode = { .renderer = renderer, .connector = connector,
			      .timeout = timeout,
			      .backoff = rc_backoff_new () };
  mode.view = rc_view_new (renderer, (struct rc_view_functions)
			   { stop_main_loop, send_message, &mode });
  open_connection (&mode, stream);
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

/* Make STREAM the connection of MODE.  */
static void
open_connection (struct window_mode *mode, struct rc_stream *stream)
{
  int fd = rc_stream_fd (stream);
  struct connection *connection = g_new (struct connection, 1);
  *connection = (struct connection)
    { .stream = stream, .reader = rc_reader_new (show_message, mode),
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
  rc_stream_close (connection->stream);
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
  struct rc_stream *stream = mode->connector.connect (mode->connector.data);
  if (!stream)
    schedule_retry (mode);
  else
    open_connection (mode, stream);
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
  (void) fd;
  (void) condition;
  struct window_mode *mode = data;
  enum read_result result = read_chunk (mode->connection->reader,
					mode->connection->stream);
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
  if (!write_all (connection->stream, bytes, size, limit))
    {
      fprintf (stderr, "remote-client: a write to the server failed\n");
      connection->broken = true;
    }
  g_free (bytes);
}

/* Write the SIZE BYTES to STREAM, whose socket does not block.  When
   the stream cannot take more bytes now, wait until it can, but at
   most LIMIT milliseconds in all.  Return false on an error or
   after LIMIT.  An input message is small, so the wait is short on a
   live connection.  */
static bool
write_all (struct rc_stream *stream, const unsigned char *bytes,
	   size_t size, int64_t limit)
{
  int64_t end = now_ms () + limit;
  while (size > 0)
    {
      ptrdiff_t n = rc_stream_write (stream, bytes, size);
      if (n == RC_STREAM_WAIT)
	{
	  int64_t left = end - now_ms ();
	  struct pollfd ready = { .fd = rc_stream_fd (stream),
				  .events = POLLOUT };
	  if (left <= 0 || poll (&ready, 1, left) == 0)
	    return false;
	  continue;
	}
      if (n < 0)
	return false;
      bytes += n;
      size -= n;
    }
  return true;
}

/* Read one chunk from STREAM, and give it to READER.  Write an error
   to the standard error output.  */
static enum read_result
read_chunk (struct rc_reader *reader, struct rc_stream *stream)
{
  unsigned char buffer[READ_SIZE];
  ptrdiff_t n = rc_stream_read (stream, buffer, sizeof buffer);
  if (n == 0)
    return READ_END;
  if (n == RC_STREAM_WAIT)
    return READ_WAIT;
  if (n < 0)
    {
      fprintf (stderr, "remote-client: a read from the server failed\n");
      return READ_ERROR;
    }
  if (!rc_reader_feed (reader, buffer, n))
    {
      fprintf (stderr, "remote-client: %s\n", rc_reader_error (reader));
      return READ_ERROR;
    }
  return READ_MORE;
}

/* The connector of a session socket: DATA is the name of the
   socket.  */
static struct rc_stream *
connect_socket (void *data)
{
  int fd = rc_connect_unix (data, false);
  return fd < 0 ? NULL : rc_stream_socket (fd);
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
	   "--timeout=MS] SOCKET\n"
	   "       remote-client [--timeout=MS] [--ssh-command=PROGRAM] "
	   "--ssh HOST\n"
	   "       remote-client --relay SOCKET\n");
  exit (1);
}
