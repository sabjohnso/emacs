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
	  remote-client [--timeout=MS] [--ssh-command=PROGRAM]
			[--listen=SPEC] [--connect=ADDRESS] [--session=NAME]
			--ssh HOST
	  remote-client --relay [--listen=SPEC] SOCKET

   The program connects to the Unix-domain socket SOCKET of a session,
   and shows each frame of the session in a window.  When the user
   asks to close a window, the program asks the server to close its
   frame, and the server decides, as Emacs does for a window of X.  The
   program stops when the connection ends and the program has no
   window, or when the user asks to close a window while the program
   has no connection.

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
   SSH, and connects to it through the relay of relay.h: see
   bootstrap.h and direct.h.  PROGRAM is the SSH program, "ssh" by
   default.  The Emacs server on HOST must run.  The relay listens at
   the place of the listen spec SPEC of listenspec.h, or on the server
   address of SSH by default.  The program connects to the relay at
   ADDRESS, or by default at the host name that "ssh -G HOST" gives.
   With --session=NAME, the program resumes the open session NAME on
   HOST, and does not start a new session.  NAME has letters, digits,
   and "-" characters, as sshargs.h says.

   With --relay, the program is the relay of relay.h for the session
   socket SOCKET.  It opens no window.  It listens at the place of the
   listen spec SPEC of listenspec.h.  With no --listen, it listens on
   the server address of $SSH_CONNECTION, which SSH sets.

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
#include "direct.h"
#include "fonts.h"
#include "fontstore.h"
#include "hashcache.h"
#include "hashfonts.h"
#include "link.h"
#include "rdchunk.h"
#include "listenspec.h"
#include "backoff.h"
#include "reader.h"
#include "relay.h"
#include "render.h"
#include "sshargs.h"
#include "stream.h"
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
   session socket.  SSH is the program of --ssh-command.  LISTEN is
   the spec of --listen, if HAS_LISTEN.  CONNECT is the address of
   --connect, or NULL.  SESSION is the name of --session, or NULL.  */
struct options
{
  const char *png;
  int64_t timeout;
  const char *ssh;
  const char *host;
  const char *socket;
  bool has_listen;
  struct rc_listen_spec listen;
  const char *connect;
  const char *session;
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
   the liveness, and RETRY is the timer of the next connect, or 0.
   STORE keeps the font files that the server sends.  */
struct window_mode
{
  struct rc_renderer *renderer;
  struct rc_font_store *store;
  struct rc_view *view;
  struct rc_connector connector;
  int64_t timeout;
  struct connection *connection;
  struct rc_backoff backoff;
  guint tick;
  guint retry;
  /* True if the last connection had an error.  */
  bool failed;
  /* The clipboard text that comes from the server, the clipboard text
     that goes to it, and the last text that went between the client and
     the server, or NULL.  The client does not send that text back.
     CLIPBOARD_WATCH sends the next message of CLIPBOARD_OUT when the
     socket has room, or is 0.  Thus the program draws and reads input
     between the messages of a large text.  The write of one message
     can still wait if the room is less than the message.  */
  struct rd_chunks clipboard;
  struct rd_chunk_queue clipboard_out;
  guint clipboard_watch;
  char *clipboard_text;
};

static struct options parse_options (int, char **);
static struct rc_stream *open_session (const struct options *,
				       struct rc_connector *,
				       struct rc_direct **);
static struct rc_stream *open_ssh_session (const struct options *,
					   struct rc_connector *,
					   struct rc_direct **);
static const char *relay_address (const struct options *,
				  char[RC_SSH_HOSTNAME_SIZE]);
static struct rc_font_source client_fonts (struct rc_font_store *);
static char *cache_file_name (const char *);
static void warn_fallback (const struct rc_font_key *);
static int render_to_file (struct rc_renderer *, struct rc_stream *,
			   const char *);
static void record_first_frame (void *, const struct rd_message *);
static int show_frames (struct rc_renderer *, struct rc_font_store *,
			struct rc_stream *, struct rc_connector, int64_t);
static struct rc_stream *connect_socket (void *);
static void open_connection (struct window_mode *, struct rc_stream *);
static void lose_connection (struct window_mode *, bool);
static void close_connection (struct window_mode *);
static void schedule_retry (struct window_mode *);
static gboolean retry_connect (gpointer);
static gboolean tick_link (gpointer);
static void network_changed (GNetworkMonitor *, gboolean, gpointer);
static void show_message (void *, const struct rd_message *);
static void want_font (struct window_mode *, const struct rd_define_font *);
static void receive_font (struct window_mode *,
			  const struct rd_font_data *);
static void receive_clipboard (struct window_mode *,
			       const struct rd_message *);
static void clipboard_changed (void *, const char *);
static gboolean send_clipboard (gint, GIOCondition, gpointer);
static gboolean read_input (gint, GIOCondition, gpointer);
static void note_input (struct window_mode *);
static void close_frame (void *, uint32_t);
static void send_message (void *, const struct rd_message *);
static void send_on (struct connection *, const struct rd_message *,
		     int64_t);
static bool write_all (struct rc_stream *, const unsigned char *, size_t,
		       int64_t);
static enum read_result read_chunk (struct rc_reader *, struct rc_stream *);
static int64_t now_ms (void);
static int relay_main (int, char **);
static void usage (void);

int
main (int argc, char **argv)
{
  if (argc > 1 && strcmp (argv[1], "--relay") == 0)
    return relay_main (argc, argv);
  struct options options = parse_options (argc, argv);
  struct rc_connector connector;
  struct rc_direct *direct = NULL;
  struct rc_stream *stream = open_session (&options, &connector, &direct);
  if (!stream)
    {
      rc_direct_free (direct);
      return 1;
    }
  char *fonts_directory = cache_file_name ("fonts");
  struct rc_font_store *store = rc_font_store_new (fonts_directory);
  g_free (fonts_directory);
  struct rc_font_source fonts = client_fonts (store);
  struct rc_renderer *renderer = rc_renderer_new (fonts);
  int status;
  if (options.png)
    status = render_to_file (renderer, stream, options.png);
  else
    {
      gtk_init (&argc, &argv);
      status = show_frames (renderer, store, stream, connector,
			    options.timeout);
    }
  rc_renderer_free (renderer);
  rc_font_source_free (fonts);
  rc_font_store_free (store);
  rc_direct_free (direct);
  return status;
}

/* Run the relay of the command line "--relay [--listen=SPEC] SOCKET"
   in ARGV with ARGC arguments.  Return the exit status.  */
static int
relay_main (int argc, char **argv)
{
  bool listen = argc == 4 && strncmp (argv[2], "--listen=", 9) == 0;
  if (argc != 3 + listen || strncmp (argv[argc - 1], "--", 2) == 0)
    usage ();
  struct rc_listen_spec spec;
  const char *connection = getenv ("SSH_CONNECTION");
  if (listen && !rc_listen_spec_parse (argv[2] + 9, &spec))
    {
      fprintf (stderr, "remote-client: %s: not a listen spec\n",
	       argv[2] + 9);
      return 1;
    }
  if (!listen
      && !(connection && rc_listen_spec_from_connection (connection, &spec)))
    {
      fprintf (stderr, "remote-client: no --listen, and no valid "
	       "SSH_CONNECTION\n");
      return 1;
    }
  return rc_relay_run (argv[argc - 1], &spec);
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
    else if (strncmp (argv[arg], "--listen=", 9) == 0)
      {
	options.has_listen = true;
	if (!rc_listen_spec_parse (argv[arg] + 9, &options.listen))
	  {
	    fprintf (stderr, "remote-client: %s: not a listen spec\n",
		     argv[arg] + 9);
	    exit (1);
	  }
      }
    else if (strncmp (argv[arg], "--connect=", 10) == 0)
      options.connect = argv[arg] + 10;
    else if (strncmp (argv[arg], "--session=", 10) == 0)
      {
	options.session = argv[arg] + 10;
	if (!rc_ssh_session_name_p (options.session))
	  {
	    fprintf (stderr, "remote-client: %s: not a name of a session\n",
		     options.session);
	    exit (1);
	  }
      }
    else
      usage ();
  if (options.host ? arg != argc || options.png
      : (arg != argc - 1 || options.has_listen || options.connect
	 || options.session))
    usage ();
  options.socket = options.host ? NULL : argv[arg];
  return options;
}

/* Connect to the session of OPTIONS, and return the stream, or NULL
   with a message.  Put the connector for later connections in
   *CONNECTOR.  With --ssh, put the direct connector in *DIRECT.  */
static struct rc_stream *
open_session (const struct options *options, struct rc_connector *connector,
	      struct rc_direct **direct)
{
  if (options->host)
    return open_ssh_session (options, connector, direct);
  *connector = (struct rc_connector) { connect_socket,
				       (void *) options->socket };
  int fd = rc_connect_unix (options->socket, true);
  return fd < 0 ? NULL : rc_stream_socket (fd);
}

/* Start a session on the host of OPTIONS through SSH, and return the
   first stream to its relay, or NULL with a message.  Put the direct
   connector in *CONNECTOR and in *DIRECT.  The address of the relay
   comes before the bootstrap, so an error there starts no session.  */
static struct rc_stream *
open_ssh_session (const struct options *options,
		  struct rc_connector *connector, struct rc_direct **direct)
{
  char hostname[RC_SSH_HOSTNAME_SIZE];
  const char *address = relay_address (options, hostname);
  if (!address)
    return NULL;
  struct rc_relay_line line;
  if (!rc_bootstrap (options->ssh, options->host,
		     options->has_listen ? &options->listen : NULL,
		     options->session, &line))
    return NULL;
  *direct = rc_direct_new (address, &line);
  if (!*direct)
    {
      perror ("remote-client");
      return NULL;
    }
  *connector = (struct rc_connector) { rc_direct_connect, *direct };
  struct rc_stream *stream = rc_direct_connect (*direct);
  if (!stream)
    fprintf (stderr, "remote-client: %s: cannot connect to the relay\n",
	     address);
  return stream;
}

/* Return the address of the relay of OPTIONS: the address of
   --connect, or else the host name of "ssh -G", which goes in
   HOSTNAME.  Return NULL with a message if "ssh -G" gives none.  */
static const char *
relay_address (const struct options *options,
	       char hostname[RC_SSH_HOSTNAME_SIZE])
{
  if (options->connect)
    return options->connect;
  return rc_bootstrap_hostname (options->ssh, options->host, hostname)
    ? hostname : NULL;
}

/* Return the font source of the client: the file of a font from
   STORE, or the font file with its hash from the font files of
   fontconfig.  Only a font whose file the server could not read gets
   the fallback font.  The server sends a file that the client does
   not have, so the client does not draw a font with the glyphs of
   another font.  */
static struct rc_font_source
client_fonts (struct rc_font_store *store)
{
  char **names = rc_font_file_names ();
  char *cache = cache_file_name ("font-hashes");
  struct rc_font_source hashed
    = rc_hash_font_source_new ((const char *const *) names,
			       g_strv_length (names), cache, rc_sha256_file,
			       NULL);
  g_strfreev (names);
  g_free (cache);
  struct rc_font_source local
    = rc_font_source_chain (hashed,
			    rc_unhashed_font_source
			    (rc_fallback_font_source_new ()),
			    warn_fallback);
  return rc_font_source_chain (rc_font_store_source (store), local, NULL);
}

/* Return the name of the file NAME in the cache directory of the
   client, in a new string.  */
static char *
cache_file_name (const char *name)
{
  return g_build_filename (g_get_user_cache_dir (), "remote-client", name,
			   NULL);
}

/* The function of the font chain when no font file of fontconfig has
   the hash of KEY.  A hash with fewer than RD_HASH_SIZE bytes means
   that the server could not read its font file.  */
static void
warn_fallback (const struct rc_font_key *key)
{
  if (key->nhash < RD_HASH_SIZE)
    {
      fprintf (stderr, "remote-client: the server could not read a font "
	       "file, so the fallback font draws its glyphs\n");
      return;
    }
  char hex[2 * RD_HASH_SIZE + 1];
  rc_hash_to_hex (key->hash, hex);
  fprintf (stderr, "remote-client: no font file has the hash %s (face "
	   "%lu), so the client asks the server for the file\n", hex,
	   (unsigned long) key->index);
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
   again after the loss of a connection with TIMEOUT.  Stop when a
   connection ends and no window is left, or when the user asks to
   close a window with no connection.  Return the exit status.  */
static int
show_frames (struct rc_renderer *renderer, struct rc_font_store *store,
	     struct rc_stream *stream, struct rc_connector connector,
	     int64_t timeout)
{
  struct window_mode mode = { .renderer = renderer, .store = store,
			      .connector = connector, .timeout = timeout,
			      .backoff = rc_backoff_new () };
  mode.view = rc_view_new (renderer, (struct rc_view_functions)
			   { close_frame, send_message, clipboard_changed,
			     &mode });
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
  rd_chunks_free (&mode.clipboard);
  rd_chunk_queue_clear (&mode.clipboard_out);
  g_free (mode.clipboard_text);
  return mode.failed ? 1 : 0;
}

/* Make STREAM the connection of MODE.  The server sends a font file
   only on the connection of its request, so forget the requests.  Send
   the clipboard text to the new connection, and drop a clipboard text
   that did not come complete.  */
static void
open_connection (struct window_mode *mode, struct rc_stream *stream)
{
  rc_font_store_reset (mode->store);
  rd_chunks_free (&mode->clipboard);
  g_free (mode->clipboard_text);
  mode->clipboard_text = NULL;
  int fd = rc_stream_fd (stream);
  struct connection *connection = g_new (struct connection, 1);
  *connection = (struct connection)
    { .stream = stream, .reader = rc_reader_new (show_message, mode),
      .link = rc_link_new (now_ms (), mode->timeout) };
  fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
  connection->watch = g_unix_fd_add (fd, G_IO_IN | G_IO_HUP | G_IO_ERR,
				     read_input, mode);
  mode->connection = connection;
  rc_view_request_clipboard (mode->view);
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
  if (mode->clipboard_watch)
    g_source_remove (mode->clipboard_watch);
  mode->clipboard_watch = 0;
  rd_chunk_queue_clear (&mode->clipboard_out);
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
   struct window_mode.  Apply MESSAGE, and show its change.  Ask the
   server for the file of a font that has no face, and keep the bytes
   of a font file.  */
static void
show_message (void *data, const struct rd_message *message)
{
  struct window_mode *mode = data;
  rc_view_update (mode->view, rc_apply (mode->renderer, message));
  if (message->type == RD_DEFINE_FONT)
    want_font (mode, &message->u.define_font);
  else if (message->type == RD_FONT_DATA)
    receive_font (mode, &message->u.font_data);
  else if (message->type == RD_CLIPBOARD_DATA)
    receive_clipboard (mode, message);
}

/* Add the clipboard-data MESSAGE to the clipboard text of MODE that
   comes from the server.  Put a complete text on the clipboard of the
   desktop.  */
static void
receive_clipboard (struct window_mode *mode,
		   const struct rd_message *message)
{
  struct rd_chunks *chunks = &mode->clipboard;
  struct rd_chunk_part part;
  if (!rd_chunk_part_of (message, &part)
      || (rd_chunks_add (chunks, &part, RD_MAX_CLIPBOARD_SIZE)
	  != RD_CHUNK_DONE))
    return;
  g_free (mode->clipboard_text);
  mode->clipboard_text = g_strndup ((const char *) chunks->bytes,
				    chunks->total);
  rc_view_set_clipboard (mode->view, (const char *) chunks->bytes,
			 chunks->total);
}

/* The function of the view for the TEXT of the clipboard of the
   desktop.  DATA is the struct window_mode.  Put a new text in the
   queue of the server, if the client has a connection.  The text
   replaces the rest of the text before it.  */
static void
clipboard_changed (void *data, const char *text)
{
  struct window_mode *mode = data;
  if (!text || !mode->connection
      || (mode->clipboard_text && strcmp (text, mode->clipboard_text) == 0))
    return;
  size_t size = strlen (text);
  if (size > RD_MAX_CLIPBOARD_SIZE
      || !rd_chunk_queue_set (&mode->clipboard_out,
			      (const unsigned char *) text, size))
    return;
  g_free (mode->clipboard_text);
  mode->clipboard_text = g_strdup (text);
  if (!mode->clipboard_watch)
    mode->clipboard_watch
      = g_unix_fd_add (rc_stream_fd (mode->connection->stream), G_IO_OUT,
		       send_clipboard, mode);
}

/* The function of the GTK main loop when the socket of the connection
   has room.  DATA is the struct window_mode.  Send the next message of
   the clipboard text, and stop at its end.  close_connection removes
   this watch, so the connection exists.  */
static gboolean
send_clipboard (gint fd, GIOCondition condition, gpointer data)
{
  (void) fd, (void) condition;
  struct window_mode *mode = data;
  struct rd_message message;
  if (rd_chunk_queue_next (&mode->clipboard_out, &message))
    send_on (mode->connection, &message, mode->timeout);
  if (rd_chunk_queue_pending_p (&mode->clipboard_out))
    return G_SOURCE_CONTINUE;
  mode->clipboard_watch = 0;
  return G_SOURCE_REMOVE;
}

/* Send a want-font message for the file of DEFINE, if the renderer of
   MODE has no face for it, and the store did not ask for it.  */
static void
want_font (struct window_mode *mode, const struct rd_define_font *define)
{
  struct rc_font_key key = { define->hash, define->nhash, define->index,
			     define->file_size };
  if (rc_renderer_has_font (mode->renderer, define->font)
      || !rc_font_store_want (mode->store, &key))
    return;
  struct rd_message message = { .type = RD_WANT_FONT,
				.u.want_font = { .nhash = RD_HASH_SIZE } };
  memcpy (message.u.want_font.hash, define->hash, RD_HASH_SIZE);
  send_on (mode->connection, &message, mode->timeout);
}

/* Give the bytes of DATA to the store of MODE.  The server defines the
   fonts of the file again after the last bytes, so the renderer finds
   the file then.  */
static void
receive_font (struct window_mode *mode, const struct rd_font_data *data)
{
  if (rc_font_store_receive (mode->store, data) == RC_FONT_REFUSED)
    fprintf (stderr, "remote-client: a font file from the server is not "
	     "correct, so the client does not draw its glyphs\n");
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

/* The function of the view when the user asks to close the window of
   FRAME.  DATA is the struct window_mode.  With a connection, ask the
   server to close FRAME: the server deletes the frame, and then the
   view removes the window.  With no connection, the server cannot
   answer, so stop the program.  The session stays on the server.  */
static void
close_frame (void *data, uint32_t frame)
{
  struct window_mode *mode = data;
  if (mode->connection)
    {
      struct rd_message message = { .type = RD_CLOSE_FRAME,
				    .u.close_frame = { .frame = frame } };
      send_on (mode->connection, &message, mode->timeout);
      return;
    }
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
	   "       remote-client [--timeout=MS] [--ssh-command=PROGRAM]\n"
	   "                     [--listen=SPEC] [--connect=ADDRESS] "
	   "[--session=NAME]\n"
	   "                     --ssh HOST\n"
	   "       remote-client --relay [--listen=SPEC] SOCKET\n");
  exit (1);
}
