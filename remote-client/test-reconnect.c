/* Test the reconnect of remote-client.

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

/* The test starts "remote-client --timeout=TIMEOUT SOCKET" with a
   socket of the test, and acts as the server.  The program shows
   windows, so the check of the Makefile runs this test in Xvfb.  Each
   example first defines a frame, so the program has a window.

   - Ping: when the server sends nothing, the program sends a ping
     after its start, and a second ping before the timeout.  The
     serial of the second ping is greater.  The wait for the first
     ping allows for the start of GTK (CONNECT_LIMIT).
   - Live: when the server answers each ping, the program keeps its
     connection for several timeouts after the first ping.  The
     serials of the pings increase.
   - Silent: when the server answers no ping, the program connects
     again, and keeps its window.
   - Closed: when the server closes the connection, the program
     connects again, and keeps its window.
   - Retry: when a connect fails, the program tries again later.
   - Reset: after the server sends bytes on a new connection, the
     next wait of the backoff is RC_BACKOFF_FIRST again.  With no
     reset, the third wait is 4 * RC_BACKOFF_FIRST.
   - End: when the server removes the last frame and then closes the
     connection, the program exits with the status 0.
   - Error: when a message with an error comes and the program has no
     window, the program exits with the status 1.
   - Close: when the user asks to close the window, the program sends
     the close-frame message of its frame, keeps its window, and runs
     on.  The test sends the request of a window manager through X.
   - Offline close: when the user asks to close the window while the
     program has no connection, the program exits with the status 0.
   - Font: when a define-font message has a full hash of a file that
     the program does not have, the program sends a want-font message
     with the hash, one time, also for a second font of the file.  The
     font-data messages of the file put the file in
     $XDG_CACHE_HOME/remote-client/fonts, with the hash in hexadecimal
     as its name.  After the program connects again, a define-font
     message for a file that did not come gives a new want-font
     message.  A define-font message for the monospace font file of
     fontconfig gives no want-font message, because the program has
     the file.
   - Clipboard: when another program puts a text on the clipboard, the
     program sends the text in clipboard-data messages.  When the
     server sends a text, the program puts it on the clipboard, and
     does not send it back.  After the program connects again, it
     sends the text of the clipboard.  The texts need several messages.
     The test runs itself as the other program: "test-reconnect
     clipboard-owner TEXT" owns the clipboard with TEXT, and
     "test-reconnect clipboard-read" writes the text of the clipboard.
   - Places: a child frame shows in the window of its root.  After
     the program connects again, a child frame that the server does
     not place again does not show.  The test reads the pixels of the
     window through X.
   - Menu choice: the program shows the menu of the menu-item and
     show-menu messages.  The keys Down and Return select its first
     item, and the program sends the menu-choice message of that item.
     A show-menu message with no entries gives a dismissal at once.
   - Menu cancel: the key Escape closes the menu, and the program
     sends a dismissal.
   - Menu hide: a hide-menu message closes the menu, and the program
     sends no choice.
   - Menu loss: the loss of the connection closes the menu, and the
     program sends no choice on the next connection.
     The menu examples send keys through the XTEST extension of X.
   - Closed write: the server sends a define-font message for a file
     that the program does not have, then removes the last frame, and
     closes the connection before the program reads.  The write of
     the want-font message then fails, because the server closed the
     connection.  The program still reads the messages before the
     close, and exits with the status 0.  The test stops the program
     with SIGSTOP while it sends and closes, so the order is certain.
   - Scale: the program runs with GDK_SCALE=2, as on a desktop with
     the scale 2.  After it connects, it sends the scale message of
     the scale 2, also after it connects again.  It paints each pixel
     of a frame on one pixel of X: a child frame at a device position
     shows at that position of X.  When the server defines the frame
     with a new size, the window of X gets that size in device
     pixels.
   - Usage: a timeout that is not a number, or that is less than
     RC_LINK_PINGS_PER_TIMEOUT, makes the program exit with the
     status 1.  */

#include <config.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "backoff.h"
#include "rdchunk.h"
#include "rdproto.h"

/* The timeout of the program in the examples, in milliseconds.  */
enum { TIMEOUT = 600 };

/* The longest wait for a connection of the program: the timeout, the
   first wait of the backoff, and a margin for a slow machine.  The
   first connection also waits for the start of GTK.  */
enum { CONNECT_LIMIT = TIMEOUT + RC_BACKOFF_FIRST + 5000 };

/* The longest wait for the exit of the program.  */
enum { EXIT_LIMIT = 10000 };

/* The frame of each example.  */
enum { FRAME = 7 };

/* The size of the font files of the font example, and of each chunk
   of them.  The bytes are not a font: the program does not draw
   them.  */
enum { FONT_SIZE = 3000, FONT_CHUNK = 1000 };

/* The wait for a message that must not come, in milliseconds.  */
enum { QUIET_LIMIT = 300 };

/* The timeout of the program in the clipboard example, in
   milliseconds.  Each message from the test starts the timeout again.
   The longest silence of the test is one wait of CONNECT_LIMIT and
   then QUIET_LIMIT.  The timeout is two times that silence.  */
enum { CLIPBOARD_TIMEOUT = 2 * (CONNECT_LIMIT + QUIET_LIMIT) };

/* One run of the program: its process, the listening socket, and the
   current connection.  */
struct run
{
  char directory[64];
  char socket[100];
  pid_t pid;
  int listener;
  int connection;
};

static bool ping_example (void);
static bool live_example (void);
static bool silent_example (void);
static bool closed_example (void);
static bool retry_example (void);
static bool reset_example (void);
static bool end_example (void);
static bool error_example (void);
static bool close_example (void);
static bool offline_close_example (void);
static bool font_example (void);
static bool clipboard_example (void);
static bool places_example (void);
static bool send_child (int);
static bool send_clear (int, uint32_t, uint32_t, uint32_t, uint32_t);
static bool wait_for_color (pid_t, int, int, uint32_t);
static uint32_t window_pixel (Display *, Window, int, int);
static bool menu_choice_example (void);
static bool menu_cancel_example (void);
static bool menu_hide_example (void);
static bool menu_loss_example (void);
static bool scale_example (void);
static bool closed_write_example (void);
static bool start_scaled_run (struct run *);
static bool wait_for_scale (int, int, uint8_t);
static bool wait_for_window_size (pid_t, int, int);
static bool send_menu (int, uint32_t);
static bool send_show_menu (int, uint32_t);
static bool wait_for_choice (int, int, uint32_t, bool, uint32_t);
static bool read_choice (int, int, struct rd_menu_choice *);
static bool menu_shows (bool);
static bool menu_window_p (Display *);
static int ignore_x_error (Display *, XErrorEvent *);
static bool press_keys (const KeySym *, int);
static char *make_text (size_t, char);
static pid_t start_clipboard_owner (const char *);
static bool send_clipboard (int, uint32_t, const char *);
static bool wait_for_clipboard (int, int, const char *);
static bool clipboard_becomes (const char *, int);
static char *read_clipboard (void);
static int clipboard_owner (const char *);
static int clipboard_read (void);
static void make_font (uint8_t *, uint8_t *, uint8_t);
static bool send_define_font (int, uint32_t, const uint8_t *);
static bool send_define_sized_font (int, uint32_t, const uint8_t *,
				    uint32_t);
static bool local_font (uint8_t *, uint32_t *);
static bool send_font_data (int, const uint8_t *, const uint8_t *);
static bool wait_for_want_font (int, int, const uint8_t *);
static bool stored_p (const struct run *, const uint8_t *, const uint8_t *);
static bool usage_example (void);
static pid_t start_program (const struct run *, const char *);
static bool start_run (struct run *);
static bool start_run_with_timeout (struct run *, int);
static void stop_clipboard_owner (pid_t);
static void stop_run (struct run *);
static bool accept_next (struct run *);
static bool alive_p (const struct run *);
static bool send_message (int, const struct rd_message *);
static bool send_define_frame (int);
static bool wait_for_start (int, uint32_t *);
static bool send_pong (int, uint32_t);
static int read_message (int, struct rd_message *, int);
static bool wait_for_ping (int, int, uint32_t *);
static int wait_for_exit (pid_t);
static int listen_at (const char *);
static int64_t now_ms (void);
static bool close_window (pid_t);
static Window wait_for_window (Display *, pid_t);
static Window find_window (Display *, Window, pid_t);
static pid_t window_pid (Display *, Window);
static bool wait_for_close_frame (int, int);

/* An example and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} examples[] =
  {
    { "ping", ping_example },
    { "live", live_example },
    { "silent", silent_example },
    { "closed", closed_example },
    { "retry", retry_example },
    { "reset", reset_example },
    { "end", end_example },
    { "error", error_example },
    { "close", close_example },
    { "offline close", offline_close_example },
    { "font", font_example },
    { "clipboard", clipboard_example },
    { "places", places_example },
    { "menu choice", menu_choice_example },
    { "menu cancel", menu_cancel_example },
    { "menu hide", menu_hide_example },
    { "menu loss", menu_loss_example },
    { "scale", scale_example },
    { "closed write", closed_write_example },
    { "usage", usage_example },
  };

int
main (int argc, char **argv)
{
  if (argc == 3 && strcmp (argv[1], "clipboard-owner") == 0)
    return clipboard_owner (argv[2]);
  if (argc == 2 && strcmp (argv[1], "clipboard-read") == 0)
    return clipboard_read ();
  signal (SIGPIPE, SIG_IGN);
  for (size_t i = 0; i < sizeof examples / sizeof *examples; i++)
    if (!examples[i].check ())
      {
	fprintf (stderr, "test-reconnect: the %s example failed\n",
		 examples[i].name);
	return 1;
      }
  printf ("test-reconnect: %zu examples passed\n",
	  sizeof examples / sizeof *examples);
  return 0;
}

/* The ping example.  */
static bool
ping_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  uint32_t first, second;
  bool passed = (wait_for_start (run.connection, &first)
		 && wait_for_ping (run.connection, TIMEOUT, &second)
		 && second > first);
  stop_run (&run);
  return passed;
}

/* The live example.  */
static bool
live_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  uint32_t last;
  bool passed = (wait_for_start (run.connection, &last)
		 && send_pong (run.connection, last));
  int64_t end = now_ms () + 4 * TIMEOUT;
  while (passed && now_ms () < end)
    {
      uint32_t serial;
      passed = (wait_for_ping (run.connection, TIMEOUT, &serial)
		&& serial > last
		&& send_pong (run.connection, serial));
      last = serial;
    }
  struct pollfd other = { .fd = run.listener, .events = POLLIN };
  passed = passed && poll (&other, 1, 0) == 0 && alive_p (&run);
  stop_run (&run);
  return passed;
}

/* The silent example.  */
static bool
silent_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  bool passed = (send_define_frame (run.connection)
		 && accept_next (&run) && alive_p (&run));
  stop_run (&run);
  return passed;
}

/* The closed example.  */
static bool
closed_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  bool passed = send_define_frame (run.connection);
  shutdown (run.connection, SHUT_RDWR);
  passed = passed && accept_next (&run) && alive_p (&run);
  stop_run (&run);
  return passed;
}

/* The retry example.  The socket does not exist for a time, so the
   first connects fail.  */
static bool
retry_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  bool passed = send_define_frame (run.connection);
  close (run.listener);
  unlink (run.socket);
  shutdown (run.connection, SHUT_RDWR);
  poll (NULL, 0, 4 * RC_BACKOFF_FIRST);
  run.listener = listen_at (run.socket);
  passed = passed && accept_next (&run) && alive_p (&run);
  stop_run (&run);
  return passed;
}

/* The reset example: three losses, each after the server sent bytes.
   Each new connection comes before 3 * RC_BACKOFF_FIRST.  The first
   loss comes after the start of the program (wait_for_start).  */
static bool
reset_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  bool passed = true;
  uint32_t serial;
  for (int loss = 0; passed && loss < 3; loss++)
    {
      passed = (loss == 0 ? wait_for_start (run.connection, &serial)
		: send_define_frame (run.connection));
      poll (NULL, 0, 100);
      int64_t start = now_ms ();
      shutdown (run.connection, SHUT_RDWR);
      passed = (passed && accept_next (&run)
		&& now_ms () - start < 3 * RC_BACKOFF_FIRST);
    }
  stop_run (&run);
  return passed;
}

/* The error example.  The length of the message is more than
   RD_MAX_LENGTH.  */
static bool
error_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  static const unsigned char bad[] = { 0xff, 0xff, 0xff, 0xff, 1 };
  bool passed = send (run.connection, bad, sizeof bad, MSG_NOSIGNAL) > 0;
  passed = passed && wait_for_exit (run.pid) == 1;
  run.pid = 0;
  stop_run (&run);
  return passed;
}

/* The usage example.  The program must exit before it connects.  */
static bool
usage_example (void)
{
  static const char *const options[] =
    { "--timeout=", "--timeout=abc", "--timeout=2", "--timeout=-5",
      "--timeout=100x" };
  struct run run;
  bool passed = start_run (&run);
  for (size_t i = 0; passed && i < sizeof options / sizeof *options; i++)
    passed = wait_for_exit (start_program (&run, options[i])) == 1;
  stop_run (&run);
  return passed;
}

/* The end example.  */
static bool
end_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  struct rd_message remove = { .type = RD_DELETE_FRAME,
			       .u.delete_frame = { .frame = FRAME } };
  bool passed = (send_define_frame (run.connection)
		 && send_message (run.connection, &remove));
  close (run.connection);
  run.connection = -1;
  passed = passed && wait_for_exit (run.pid) == 0;
  run.pid = 0;
  stop_run (&run);
  return passed;
}

/* The close example.  */
static bool
close_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  uint32_t serial;
  bool passed = (send_define_frame (run.connection)
		 && close_window (run.pid)
		 && wait_for_close_frame (run.connection, CONNECT_LIMIT)
		 && wait_for_ping (run.connection, CONNECT_LIMIT, &serial)
		 && alive_p (&run));
  stop_run (&run);
  return passed;
}

/* The offline close example.  The socket does not exist after the
   loss, so the program cannot connect again.  */
static bool
offline_close_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  bool passed = send_define_frame (run.connection);
  close (run.listener);
  unlink (run.socket);
  shutdown (run.connection, SHUT_RDWR);
  poll (NULL, 0, 200);
  passed = passed && close_window (run.pid) && wait_for_exit (run.pid) == 0;
  run.pid = 0;
  run.listener = listen_at (run.socket);
  stop_run (&run);
  return passed;
}

/* The child frame of the places example, its place in FRAME, its
   size, and the colors of the example.  */
enum { CHILD = 8, CHILD_X = 20, CHILD_Y = 10, CHILD_WIDTH = 30,
       CHILD_HEIGHT = 20, ROOT_COLOR = 0xff0000, CHILD_COLOR = 0x0000ff };

/* The scale example.  */
static bool
scale_example (void)
{
  struct run run;
  if (!start_scaled_run (&run))
    return false;
  int x = CHILD_X + 5, y = CHILD_Y + 5;
  bool passed = (wait_for_scale (run.connection, CONNECT_LIMIT, 2)
		 && send_define_frame (run.connection)
		 && send_clear (run.connection, FRAME, 200, 100, ROOT_COLOR)
		 && send_child (run.connection)
		 && wait_for_color (run.pid, x, y, CHILD_COLOR));
  struct rd_message define
    = { .type = RD_DEFINE_FRAME,
	.u.define_frame = { .frame = FRAME, .width = 400, .height = 240 } };
  passed = (passed && send_message (run.connection, &define)
	    && wait_for_window_size (run.pid, 400, 240));
  shutdown (run.connection, SHUT_RDWR);
  passed = (passed && accept_next (&run)
	    && wait_for_scale (run.connection, CONNECT_LIMIT, 2));
  stop_run (&run);
  return passed;
}

/* The closed write example.  */
static bool
closed_write_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  uint8_t font[FONT_SIZE], hash[RD_HASH_SIZE];
  make_font (font, hash, 3);
  struct rd_message remove = { .type = RD_DELETE_FRAME,
			       .u.delete_frame = { .frame = FRAME } };
  bool passed = (send_define_frame (run.connection)
		 && wait_for_scale (run.connection, CONNECT_LIMIT, 1));
  kill (run.pid, SIGSTOP);
  passed = (passed && send_define_font (run.connection, 1, hash)
	    && send_message (run.connection, &remove));
  close (run.connection);
  run.connection = -1;
  kill (run.pid, SIGCONT);
  passed = passed && wait_for_exit (run.pid) == 0;
  run.pid = 0;
  stop_run (&run);
  return passed;
}

/* Start a run of the program with GDK_SCALE=2 in RUN, as start_run
   does.  The other runs keep the environment of the test.  */
static bool
start_scaled_run (struct run *run)
{
  const char *old = getenv ("GDK_SCALE");
  char *saved = old ? strdup (old) : NULL;
  setenv ("GDK_SCALE", "2", 1);
  bool started = start_run (run);
  if (saved)
    setenv ("GDK_SCALE", saved, 1);
  else
    unsetenv ("GDK_SCALE");
  free (saved);
  return started;
}

/* Wait until the window of the program PID has the size WIDTH,
   HEIGHT in pixels of X, for at most CONNECT_LIMIT.  Return true if it
   has that size.  */
static bool
wait_for_window_size (pid_t pid, int width, int height)
{
  Display *display = XOpenDisplay (NULL);
  if (!display)
    return false;
  Window window = wait_for_window (display, pid);
  int64_t end = now_ms () + CONNECT_LIMIT;
  XWindowAttributes attributes = { 0 };
  while (window && XGetWindowAttributes (display, window, &attributes)
	 && (attributes.width != width || attributes.height != height)
	 && now_ms () < end)
    poll (NULL, 0, 20);
  XCloseDisplay (display);
  return attributes.width == width && attributes.height == height;
}

/* Read the messages from FD until a scale message of SCALE, for at
   most LIMIT milliseconds.  Return true if it came.  */
static bool
wait_for_scale (int fd, int limit, uint8_t scale)
{
  int64_t end = now_ms () + limit;
  struct rd_message message;
  int64_t left;
  while ((left = end - now_ms ()) > 0)
    if (read_message (fd, &message, left) == 1
	&& message.type == RD_SCALE && message.u.scale.scale == scale)
      return true;
  return false;
}

/* The places example.  */
static bool
places_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  int x = CHILD_X + 5, y = CHILD_Y + 5;
  bool passed = (send_define_frame (run.connection)
		 && send_clear (run.connection, FRAME, 200, 100, ROOT_COLOR)
		 && send_child (run.connection)
		 && wait_for_color (run.pid, x, y, CHILD_COLOR));
  shutdown (run.connection, SHUT_RDWR);
  passed = (passed && accept_next (&run)
	    && send_define_frame (run.connection)
	    && send_clear (run.connection, FRAME, 200, 100, ROOT_COLOR)
	    && wait_for_color (run.pid, x, y, ROOT_COLOR));
  stop_run (&run);
  return passed;
}

/* Place the visible child frame CHILD in FRAME on the connection FD,
   define it, and fill it with CHILD_COLOR.  */
static bool
send_child (int fd)
{
  struct rd_message place
    = { .type = RD_PLACE_FRAME,
	.u.place_frame = { .frame = CHILD, .parent = FRAME, .x = CHILD_X,
			   .y = CHILD_Y, .z = 1, .visible = 1 } };
  struct rd_message define
    = { .type = RD_DEFINE_FRAME,
	.u.define_frame = { .frame = CHILD, .width = CHILD_WIDTH,
			    .height = CHILD_HEIGHT } };
  return (send_message (fd, &place) && send_message (fd, &define)
	  && send_clear (fd, CHILD, CHILD_WIDTH, CHILD_HEIGHT, CHILD_COLOR));
}

/* Fill the area WIDTH, HEIGHT at the origin of FRAME with COLOR on the
   connection FD.  */
static bool
send_clear (int fd, uint32_t frame, uint32_t width, uint32_t height,
	    uint32_t color)
{
  struct rd_message clear
    = { .type = RD_CLEAR_AREA,
	.u.clear_area = { .frame = frame, .width = width, .height = height,
			  .color = color } };
  return send_message (fd, &clear);
}

/* Wait at most CONNECT_LIMIT milliseconds until the pixel X, Y of the
   window of the program PID has the color COLOR.  Return false if it
   does not get the color.  */
static bool
wait_for_color (pid_t pid, int x, int y, uint32_t color)
{
  Display *display = XOpenDisplay (NULL);
  if (!display)
    return false;
  Window window = wait_for_window (display, pid);
  int64_t end = now_ms () + CONNECT_LIMIT;
  bool found = false;
  while (window && !(found = window_pixel (display, window, x, y) == color)
	 && now_ms () < end)
    poll (NULL, 0, 20);
  XCloseDisplay (display);
  return found;
}

/* Return the color of the pixel X, Y of WINDOW, as 0xRRGGBB.  The
   screen of Xvfb has 24 bits of color.  */
static uint32_t
window_pixel (Display *display, Window window, int x, int y)
{
  XImage *image = XGetImage (display, window, x, y, 1, 1, AllPlanes,
			     ZPixmap);
  if (!image)
    return UINT32_MAX;
  uint32_t pixel = XGetPixel (image, 0, 0) & 0xffffff;
  XDestroyImage (image);
  return pixel;
}

/* The font example, with the files A and B.  */
static bool
font_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  uint8_t a[FONT_SIZE], a_hash[RD_HASH_SIZE];
  uint8_t b[FONT_SIZE], b_hash[RD_HASH_SIZE];
  make_font (a, a_hash, 1);
  make_font (b, b_hash, 2);
  int64_t end = now_ms () + CONNECT_LIMIT;
  bool passed = (send_define_frame (run.connection)
		 && send_define_font (run.connection, 1, a_hash)
		 && wait_for_want_font (run.connection, CONNECT_LIMIT, a_hash)
		 && send_define_font (run.connection, 2, a_hash)
		 && !wait_for_want_font (run.connection, QUIET_LIMIT, a_hash)
		 && send_font_data (run.connection, a, a_hash)
		 && send_define_font (run.connection, 3, b_hash)
		 && wait_for_want_font (run.connection, CONNECT_LIMIT,
					b_hash));
  while (passed && !stored_p (&run, a, a_hash) && now_ms () < end)
    poll (NULL, 0, 20);
  passed = passed && stored_p (&run, a, a_hash);
  shutdown (run.connection, SHUT_RDWR);
  passed = (passed && accept_next (&run)
	    && send_define_font (run.connection, 3, b_hash)
	    && wait_for_want_font (run.connection, CONNECT_LIMIT, b_hash));
  uint8_t local_hash[RD_HASH_SIZE];
  uint32_t local_size;
  passed = (passed && local_font (local_hash, &local_size)
	    && send_define_sized_font (run.connection, 4, local_hash,
				       local_size)
	    && !wait_for_want_font (run.connection, QUIET_LIMIT,
				    local_hash));
  stop_run (&run);
  return passed;
}

/* Put the hash and the size of the monospace font file of fontconfig
   in HASH and *SIZE.  Return false if there is no such file.  */
static bool
local_font (uint8_t *hash, uint32_t *size)
{
  gchar *name = NULL, *bytes = NULL;
  gsize length = 0;
  bool found = (g_spawn_command_line_sync ("fc-match -f %{file} monospace",
					   &name, NULL, NULL, NULL)
		&& g_file_get_contents (name, &bytes, &length, NULL));
  if (found)
    {
      gsize hash_length = RD_HASH_SIZE;
      GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA256);
      g_checksum_update (checksum, (const guchar *) bytes, length);
      g_checksum_get_digest (checksum, hash, &hash_length);
      g_checksum_free (checksum);
      *size = length;
    }
  g_free (bytes);
  g_free (name);
  return found;
}

/* The clipboard example, with the texts LAPTOP and SERVER.  */
static bool
clipboard_example (void)
{
  char *laptop = make_text (20000, 'L'), *server = make_text (12000, 'S');
  /* The owner starts before the program connects, and the program has
     a long timeout.  The starts of the owner and of each reader of the
     clipboard can take more than TIMEOUT, and this example sends no
     pong.  The ping and live examples check the timeout.  */
  pid_t owner = start_clipboard_owner (laptop);
  struct run run;
  const char *step = "start";
  bool passed = (start_run_with_timeout (&run, CLIPBOARD_TIMEOUT)
		 && (step = "own the clipboard", owner > 0)
		 && send_define_frame (run.connection)
		 && (step = "send the text of the laptop",
		     wait_for_clipboard (run.connection, CONNECT_LIMIT,
					 laptop)));
  stop_clipboard_owner (owner);
  struct pollfd other = { .fd = run.listener, .events = POLLIN };
  passed = (passed && send_clipboard (run.connection, 9, server)
	    && (step = "put the text of the server on the clipboard",
		clipboard_becomes (server, CONNECT_LIMIT))
	    && (step = "do not send the text of the server back",
		!wait_for_clipboard (run.connection, QUIET_LIMIT, server))
	    && (step = "keep the first connection",
		poll (&other, 1, 0) == 0 && alive_p (&run)));
  shutdown (run.connection, SHUT_RDWR);
  passed = (passed && accept_next (&run)
	    && (step = "send the clipboard after a new connection",
		wait_for_clipboard (run.connection, CONNECT_LIMIT, server)));
  if (!passed)
    fprintf (stderr, "test-reconnect: clipboard: the step \"%s\" "
	     "failed\n", step);
  free (laptop);
  free (server);
  stop_run (&run);
  return passed;
}

/* The serial of the menu of the menu examples, and the id of its
   first item.  */
enum { MENU = 3, FIRST_ITEM = 1 };

/* The menu choice example.  */
static bool
menu_choice_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  static const KeySym select[] = { XK_Down, XK_Return };
  bool passed = (send_define_frame (run.connection)
		 && send_show_menu (run.connection, MENU + 1)
		 && wait_for_choice (run.connection, CONNECT_LIMIT, MENU + 1,
				     false, 0)
		 && send_menu (run.connection, MENU) && menu_shows (true)
		 && press_keys (select, 2)
		 && wait_for_choice (run.connection, CONNECT_LIMIT, MENU, true,
				     FIRST_ITEM));
  stop_run (&run);
  return passed;
}

/* The menu cancel example.  */
static bool
menu_cancel_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  static const KeySym cancel[] = { XK_Escape };
  bool passed = (send_define_frame (run.connection)
		 && send_menu (run.connection, MENU) && menu_shows (true)
		 && press_keys (cancel, 1)
		 && wait_for_choice (run.connection, CONNECT_LIMIT, MENU, false,
				     0)
		 && menu_shows (false));
  stop_run (&run);
  return passed;
}

/* The menu hide example.  After the hide, the key Escape does not
   close a menu, so no dismissal comes.  */
static bool
menu_hide_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  static const KeySym cancel[] = { XK_Escape };
  struct rd_message hide = { .type = RD_HIDE_MENU,
			     .u.hide_menu = { .serial = MENU } };
  struct rd_menu_choice choice;
  bool passed = (send_define_frame (run.connection)
		 && send_menu (run.connection, MENU) && menu_shows (true)
		 && send_message (run.connection, &hide) && menu_shows (false)
		 && press_keys (cancel, 1)
		 && !read_choice (run.connection, QUIET_LIMIT, &choice));
  stop_run (&run);
  return passed;
}

/* The menu loss example.  The socket does not exist for a time, so the
   menu must close at the loss, and not at the next connection.  */
static bool
menu_loss_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  static const KeySym cancel[] = { XK_Escape };
  struct rd_menu_choice choice;
  bool passed = (send_define_frame (run.connection)
		 && send_menu (run.connection, MENU) && menu_shows (true));
  close (run.listener);
  unlink (run.socket);
  shutdown (run.connection, SHUT_RDWR);
  passed = passed && menu_shows (false);
  run.listener = listen_at (run.socket);
  passed = (passed && accept_next (&run) && press_keys (cancel, 1)
	    && !read_choice (run.connection, QUIET_LIMIT, &choice)
	    && alive_p (&run));
  stop_run (&run);
  return passed;
}

/* Send the entries of the menu SERIAL on the connection FD, and then
   its show-menu message.  The menu has a title, the item FIRST_ITEM, a
   separator, and a submenu with one item.  */
static bool
send_menu (int fd, uint32_t serial)
{
  static const struct
  {
    uint8_t kind;
    uint32_t id;
    const char *label, *key;
  } entries[] =
    {
      { RD_MENU_KIND_ITEM, FIRST_ITEM, "Open", "C-o" },
      { RD_MENU_KIND_SEPARATOR, 0, "", "" },
      { RD_MENU_KIND_SUBMENU_START, 0, "More", "" },
      { RD_MENU_KIND_ITEM, FIRST_ITEM + 1, "Close", "" },
      { RD_MENU_KIND_SUBMENU_END, 0, "", "" },
    };
  bool sent = true;
  for (size_t i = 0; sent && i < sizeof entries / sizeof *entries; i++)
    {
      struct rd_message message
	= { .type = RD_MENU_ITEM,
	    .u.menu_item = { .serial = serial, .kind = entries[i].kind,
			     .flags = RD_MENU_FLAG_ENABLED,
			     .id = entries[i].id,
			     .nlabel = strlen (entries[i].label),
			     .nkey = strlen (entries[i].key) } };
      memcpy (message.u.menu_item.label, entries[i].label,
	      message.u.menu_item.nlabel);
      memcpy (message.u.menu_item.key, entries[i].key,
	      message.u.menu_item.nkey);
      sent = send_message (fd, &message);
    }
  return sent && send_show_menu (fd, serial);
}

/* Send the show-menu message of the menu SERIAL of FRAME, with a
   title, on the connection FD.  */
static bool
send_show_menu (int fd, uint32_t serial)
{
  struct rd_message show
    = { .type = RD_SHOW_MENU,
	.u.show_menu = { .frame = FRAME, .serial = serial, .x = 10, .y = 10,
			 .ntitle = 4 } };
  memcpy (show.u.show_menu.title, "Menu", 4);
  return send_message (fd, &show);
}

/* Wait at most LIMIT milliseconds for a menu-choice message on the
   connection FD.  Return true if it comes and has SERIAL, CHOSEN and
   ID.  */
static bool
wait_for_choice (int fd, int limit, uint32_t serial, bool chosen,
		 uint32_t id)
{
  struct rd_menu_choice choice;
  return (read_choice (fd, limit, &choice) && choice.serial == serial
	  && choice.chosen == chosen && choice.id == id);
}

/* Wait at most LIMIT milliseconds for a menu-choice message on the
   connection FD, and put it in *CHOICE.  Skip the other messages.
   Return false if none comes.  */
static bool
read_choice (int fd, int limit, struct rd_menu_choice *choice)
{
  int64_t end = now_ms () + limit;
  struct rd_message message;
  int64_t left;
  while ((left = end - now_ms ()) > 0)
    if (read_message (fd, &message, left) == 1
	&& message.type == RD_MENU_CHOICE)
      {
	*choice = message.u.menu_choice;
	return true;
      }
  return false;
}

/* Wait at most CONNECT_LIMIT milliseconds until a popup menu shows on
   the display, if SHOWN, or until no popup menu shows.  Return false
   if that does not occur.  A window can go away while the test looks
   at it, so ignore the errors of X while the test waits.  */
static bool
menu_shows (bool shown)
{
  Display *display = XOpenDisplay (NULL);
  if (!display)
    return false;
  XErrorHandler old = XSetErrorHandler (ignore_x_error);
  int64_t end = now_ms () + CONNECT_LIMIT;
  bool done;
  while (!(done = menu_window_p (display) == shown) && now_ms () < end)
    poll (NULL, 0, 20);
  XCloseDisplay (display);
  XSetErrorHandler (old);
  return done;
}

/* The error handler of X while the test waits for a menu.  Ignore the
   error: the function of X that failed returns 0.  */
static int
ignore_x_error (Display *display, XErrorEvent *error)
{
  (void) display, (void) error;
  return 0;
}

/* Return true if a popup menu shows on DISPLAY: a child of the root
   window that shows and that the window manager does not manage.  The
   test runs in Xvfb, and only a menu of the program is such a
   window.  */
static bool
menu_window_p (Display *display)
{
  Window root, parent, *children;
  unsigned int count;
  if (!XQueryTree (display, DefaultRootWindow (display), &root, &parent,
		   &children, &count))
    return false;
  bool found = false;
  for (unsigned int i = 0; !found && i < count; i++)
    {
      XWindowAttributes attributes;
      found = (XGetWindowAttributes (display, children[i], &attributes)
	       && attributes.map_state == IsViewable
	       && attributes.override_redirect);
    }
  XFree (children);
  return found;
}

/* Press and release each of the N keys KEYS in order, through the
   XTEST extension.  Return false if the display has no XTEST.  */
static bool
press_keys (const KeySym *keys, int n)
{
  Display *display = XOpenDisplay (NULL);
  if (!display)
    return false;
  int event, error, major, minor;
  bool pressed = XTestQueryExtension (display, &event, &error, &major,
				      &minor);
  for (int i = 0; pressed && i < n; i++)
    {
      KeyCode code = XKeysymToKeycode (display, keys[i]);
      XTestFakeKeyEvent (display, code, True, CurrentTime);
      XTestFakeKeyEvent (display, code, False, CurrentTime);
    }
  XSync (display, False);
  XCloseDisplay (display);
  return pressed;
}

/* Return a new text of SIZE bytes or a few more, of ASCII and other
   UTF-8 characters, that starts with MARK.  Free it with free.  */
static char *
make_text (size_t size, char mark)
{
  static const char *const parts[] = { "abc ", "\xc3\xa9", "\xce\xbb ",
				       "\xf0\x9f\x98\x80", "\n" };
  char *text = malloc (size + 8);
  size_t used = 0;
  text[used++] = mark;
  for (int i = 0; used < size; i++)
    {
      const char *part = parts[i % (sizeof parts / sizeof *parts)];
      memcpy (text + used, part, strlen (part));
      used += strlen (part);
    }
  text[used] = 0;
  return text;
}

/* Start this program as the owner of the clipboard with TEXT.  Return
   its process ID when it owns the clipboard, or -1.  */
static pid_t
start_clipboard_owner (const char *text)
{
  pid_t pid = fork ();
  if (pid == 0)
    {
      execl ("./test-reconnect", "test-reconnect", "clipboard-owner", text,
	     (char *) NULL);
      _exit (127);
    }
  if (pid > 0 && !clipboard_becomes (text, CONNECT_LIMIT))
    {
      stop_clipboard_owner (pid);
      return -1;
    }
  return pid;
}

/* Stop the clipboard owner OWNER, if OWNER is a process ID.  */
static void
stop_clipboard_owner (pid_t owner)
{
  if (owner > 0)
    {
      kill (owner, SIGTERM);
      waitpid (owner, NULL, 0);
    }
}

/* Send TEXT with SERIAL in clipboard-data messages on the connection
   FD.  */
static bool
send_clipboard (int fd, uint32_t serial, const char *text)
{
  size_t size = strlen (text);
  bool sent = true;
  for (uint32_t i = 0; sent && i < rd_chunk_count (size); i++)
    {
      struct rd_message message;
      rd_chunk_message (RD_CLIPBOARD_DATA, serial,
			(const unsigned char *) text, size, i, &message);
      sent = send_message (fd, &message);
    }
  return sent;
}

/* Wait at most LIMIT milliseconds for the clipboard-data messages of
   TEXT on the connection FD.  Skip the other messages.  Return false
   if the text does not come.  */
static bool
wait_for_clipboard (int fd, int limit, const char *text)
{
  struct rd_chunks chunks = { 0 };
  int64_t end = now_ms () + limit;
  struct rd_message message;
  int64_t left;
  bool found = false;
  while (!found && (left = end - now_ms ()) > 0)
    if (read_message (fd, &message, left) == 1
	&& (rd_chunks_add_message (&chunks, &message, RD_MAX_CLIPBOARD_SIZE)
	    == RD_CHUNK_DONE))
      found = (chunks.total == strlen (text)
	       && !memcmp (chunks.bytes, text, chunks.total));
  rd_chunks_free (&chunks);
  return found;
}

/* Wait at most LIMIT milliseconds for the clipboard to have TEXT.  */
static bool
clipboard_becomes (const char *text, int limit)
{
  int64_t end = now_ms () + limit;
  bool same = false;
  while (!same && now_ms () < end)
    {
      char *now = read_clipboard ();
      same = now && strcmp (now, text) == 0;
      free (now);
      if (!same)
	poll (NULL, 0, 50);
    }
  return same;
}

/* Return the text of the clipboard in a new string, or NULL.  */
static char *
read_clipboard (void)
{
  gchar *out = NULL;
  gint status;
  char *argv[] = { "./test-reconnect", "clipboard-read", NULL };
  if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
		     &out, NULL, &status, NULL)
      || !g_spawn_check_wait_status (status, NULL))
    {
      g_free (out);
      return NULL;
    }
  char *text = strdup (out);
  g_free (out);
  return text;
}

/* Own the clipboard with TEXT until a signal stops the process.  */
static int
clipboard_owner (const char *text)
{
  gtk_init (NULL, NULL);
  gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD), text,
			  -1);
  gtk_main ();
  return 0;
}

/* Write the text of the clipboard to the standard output.  Return 1 if
   the clipboard has no text.  */
static int
clipboard_read (void)
{
  gtk_init (NULL, NULL);
  gchar *text = gtk_clipboard_wait_for_text
    (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD));
  if (!text)
    return 1;
  fputs (text, stdout);
  g_free (text);
  return 0;
}

/* Fill FONT with FONT_SIZE bytes that depend on SEED, and put their
   SHA-256 hash in HASH.  */
static void
make_font (uint8_t *font, uint8_t *hash, uint8_t seed)
{
  for (int i = 0; i < FONT_SIZE; i++)
    font[i] = seed * 31 + i * 7;
  gsize length = RD_HASH_SIZE;
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA256);
  g_checksum_update (checksum, font, FONT_SIZE);
  g_checksum_get_digest (checksum, hash, &length);
  g_checksum_free (checksum);
}

/* Send a define-font message for the font ID FONT, a file of
   FONT_SIZE bytes with the hash HASH, on the connection FD.  */
static bool
send_define_font (int fd, uint32_t font, const uint8_t *hash)
{
  return send_define_sized_font (fd, font, hash, FONT_SIZE);
}

/* Send a define-font message for the font ID FONT, a file of SIZE
   bytes with the hash HASH, on the connection FD.  */
static bool
send_define_sized_font (int fd, uint32_t font, const uint8_t *hash,
			uint32_t size)
{
  struct rd_message define
    = { .type = RD_DEFINE_FONT,
	.u.define_font = { .font = font, .size = 12, .nhash = RD_HASH_SIZE,
			   .file_size = size } };
  memcpy (define.u.define_font.hash, hash, RD_HASH_SIZE);
  return send_message (fd, &define);
}

/* Send the FONT_SIZE bytes FONT with the hash HASH in font-data
   messages of FONT_CHUNK bytes on the connection FD.  */
static bool
send_font_data (int fd, const uint8_t *font, const uint8_t *hash)
{
  bool sent = true;
  for (uint32_t offset = 0; sent && offset < FONT_SIZE;
       offset += FONT_CHUNK)
    {
      struct rd_message data
	= { .type = RD_FONT_DATA,
	    .u.font_data = { .nhash = RD_HASH_SIZE, .offset = offset,
			     .total = FONT_SIZE, .nbytes = FONT_CHUNK } };
      memcpy (data.u.font_data.hash, hash, RD_HASH_SIZE);
      memcpy (data.u.font_data.bytes, font + offset, FONT_CHUNK);
      sent = send_message (fd, &data);
    }
  return sent;
}

/* Wait at most LIMIT milliseconds for a want-font message with the
   hash HASH on the connection FD.  Skip the other messages.  Return
   false if it does not come.  */
static bool
wait_for_want_font (int fd, int limit, const uint8_t *hash)
{
  int64_t end = now_ms () + limit;
  struct rd_message message;
  int64_t left;
  while ((left = end - now_ms ()) > 0)
    if (read_message (fd, &message, left) == 1
	&& message.type == RD_WANT_FONT
	&& message.u.want_font.nhash == RD_HASH_SIZE
	&& !memcmp (message.u.want_font.hash, hash, RD_HASH_SIZE))
      return true;
  return false;
}

/* Return true if the font directory of RUN has the file with the hash
   HASH, and the file has the FONT_SIZE bytes FONT.  */
static bool
stored_p (const struct run *run, const uint8_t *font, const uint8_t *hash)
{
  char *hex = g_malloc (2 * RD_HASH_SIZE + 1);
  for (int i = 0; i < RD_HASH_SIZE; i++)
    snprintf (hex + 2 * i, 3, "%02x", hash[i]);
  char *name = g_strdup_printf ("%s/remote-client/fonts/%s",
				run->directory, hex);
  gchar *bytes = NULL;
  gsize size = 0;
  bool stored = (g_file_get_contents (name, &bytes, &size, NULL)
		 && size == FONT_SIZE && !memcmp (bytes, font, size));
  g_free (bytes);
  g_free (name);
  g_free (hex);
  return stored;
}

/* Start a run with the timeout TIMEOUT of the program.  See
   start_run_with_timeout.  */
static bool
start_run (struct run *run)
{
  return start_run_with_timeout (run, TIMEOUT);
}

/* Start the program for RUN with the timeout TIMEOUT, in
   milliseconds, and accept its first connection.  Return false if the
   program does not connect.  */
static bool
start_run_with_timeout (struct run *run, int timeout)
{
  snprintf (run->directory, sizeof run->directory,
	    "/tmp/test-reconnect-XXXXXX");
  if (!mkdtemp (run->directory))
    {
      perror ("test-reconnect: mkdtemp");
      exit (2);
    }
  snprintf (run->socket, sizeof run->socket, "%s/socket", run->directory);
  run->listener = listen_at (run->socket);
  run->connection = -1;
  char option[64];
  snprintf (option, sizeof option, "--timeout=%d", timeout);
  run->pid = start_program (run, option);
  return accept_next (run);
}

/* Start the program with OPTION and the socket of RUN, and return its
   process ID.  */
static pid_t
start_program (const struct run *run, const char *option)
{
  pid_t pid = fork ();
  if (pid == 0)
    {
      setenv ("XDG_CACHE_HOME", run->directory, 1);
      execl ("./remote-client", "remote-client", option, run->socket,
	     (char *) NULL);
      _exit (127);
    }
  return pid;
}

/* Stop the program of RUN if it runs, and remove the files of RUN.  */
static void
stop_run (struct run *run)
{
  if (run->pid > 0)
    {
      kill (run->pid, SIGKILL);
      waitpid (run->pid, NULL, 0);
    }
  if (run->connection >= 0)
    close (run->connection);
  close (run->listener);
  unlink (run->socket);
  char cache[160];
  snprintf (cache, sizeof cache, "%s/remote-client/font-hashes",
	    run->directory);
  unlink (cache);
  snprintf (cache, sizeof cache, "%s/remote-client/fonts", run->directory);
  GDir *fonts = g_dir_open (cache, 0, NULL);
  const char *name;
  while (fonts && (name = g_dir_read_name (fonts)))
    {
      char *file = g_build_filename (cache, name, NULL);
      unlink (file);
      g_free (file);
    }
  if (fonts)
    g_dir_close (fonts);
  rmdir (cache);
  snprintf (cache, sizeof cache, "%s/remote-client", run->directory);
  rmdir (cache);
  rmdir (run->directory);
}

/* Accept the next connection of the program of RUN in CONNECT_LIMIT
   milliseconds, and make it the connection of RUN.  Return false if
   no connection comes.  */
static bool
accept_next (struct run *run)
{
  struct pollfd ready = { .fd = run->listener, .events = POLLIN };
  if (poll (&ready, 1, CONNECT_LIMIT) != 1)
    return false;
  int connection = accept (run->listener, NULL, NULL);
  if (connection < 0)
    return false;
  if (run->connection >= 0)
    close (run->connection);
  run->connection = connection;
  return true;
}

/* Return true if the program of RUN runs.  */
static bool
alive_p (const struct run *run)
{
  return waitpid (run->pid, NULL, WNOHANG) == 0;
}

/* Send MESSAGE on the connection FD.  Return false on an error.  */
static bool
send_message (int fd, const struct rd_message *message)
{
  size_t size = rd_encoded_size (message);
  unsigned char *bytes = g_malloc (size);
  rd_encode (message, bytes);
  bool sent = send (fd, bytes, size, MSG_NOSIGNAL) == (ssize_t) size;
  g_free (bytes);
  return sent;
}

/* Send a define-frame message on the connection FD, and wait at most
   CONNECT_LIMIT milliseconds for the first ping of the program.  The
   program reads the message and sends the ping only after the start
   of GTK, which can take most of TIMEOUT on a slow machine.  Set
   *SERIAL to the serial of the ping.  Return false if no ping
   comes.  */
static bool
wait_for_start (int fd, uint32_t *serial)
{
  return (send_define_frame (fd)
	  && wait_for_ping (fd, CONNECT_LIMIT, serial));
}

/* Send a pong with SERIAL on the connection FD.  Return false if the
   send fails.  */
static bool
send_pong (int fd, uint32_t serial)
{
  struct rd_message pong = { .type = RD_PONG,
			     .u.pong = { .serial = serial } };
  return send_message (fd, &pong);
}

/* Define the frame FRAME on the connection FD, so the program shows
   a window.  */
static bool
send_define_frame (int fd)
{
  struct rd_message define
    = { .type = RD_DEFINE_FRAME,
	.u.define_frame = { .frame = FRAME, .width = 200, .height = 100 } };
  return send_message (fd, &define);
}

/* Wait at most LIMIT milliseconds for a ping on the connection FD.
   Skip the other messages.  Set *SERIAL to the serial of the ping.
   Return false if no ping comes.  */
static bool
wait_for_ping (int fd, int limit, uint32_t *serial)
{
  int64_t end = now_ms () + limit;
  struct rd_message message;
  int64_t left;
  while ((left = end - now_ms ()) > 0)
    if (read_message (fd, &message, left) == 1 && message.type == RD_PING)
      {
	*serial = message.u.ping.serial;
	return true;
      }
  return false;
}

/* Read one message from the connection FD into MESSAGE.  Wait at most
   LIMIT milliseconds.  Return 1 for a message, 0 for no message in
   time, and -1 at the end of the connection or on an error.  The
   input messages of the program are small, so a message comes in one
   read of its length and one read of the rest.  */
static int
read_message (int fd, struct rd_message *message, int limit)
{
  unsigned char bytes[16384];
  struct pollfd ready = { .fd = fd, .events = POLLIN };
  if (poll (&ready, 1, limit) != 1)
    return 0;
  if (recv (fd, bytes, 4, MSG_WAITALL) != 4)
    return -1;
  size_t length = ((size_t) bytes[0] << 24 | bytes[1] << 16
		   | bytes[2] << 8 | bytes[3]);
  if (length > sizeof bytes - 4
      || recv (fd, bytes + 4, length, MSG_WAITALL) != (ssize_t) length)
    return -1;
  size_t used;
  return rd_decode (bytes, length + 4, message, &used) == RD_OK ? 1 : 0;
}

/* Return the exit status of the child PID.  If it does not exit in
   EXIT_LIMIT milliseconds, kill it and return -1.  */
static int
wait_for_exit (pid_t pid)
{
  for (int waited = 0; waited < EXIT_LIMIT; waited += 10)
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

/* Return a listening socket at PATH.  */
static int
listen_at (const char *path)
{
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  snprintf (address.sun_path, sizeof address.sun_path, "%s", path);
  if (fd < 0 || bind (fd, (struct sockaddr *) &address, sizeof address) != 0
      || listen (fd, 1) != 0)
    {
      perror ("test-reconnect: listen");
      exit (2);
    }
  return fd;
}

/* Wait at most LIMIT milliseconds for the close-frame message of
   FRAME on the connection FD.  Skip the other messages.  Return false
   if it does not come.  */
static bool
wait_for_close_frame (int fd, int limit)
{
  int64_t end = now_ms () + limit;
  struct rd_message message;
  int64_t left;
  while ((left = end - now_ms ()) > 0)
    if (read_message (fd, &message, left) == 1
	&& message.type == RD_CLOSE_FRAME
	&& message.u.close_frame.frame == FRAME)
      return true;
  return false;
}

/* Ask the window of the program PID to close, as a window manager
   does: send it the WM_DELETE_WINDOW message of the WM_PROTOCOLS of X.
   Wait at most CONNECT_LIMIT milliseconds for the window to show.
   Return false if no window shows.  */
static bool
close_window (pid_t pid)
{
  Display *display = XOpenDisplay (NULL);
  if (!display)
    return false;
  Window window = wait_for_window (display, pid);
  if (window)
    {
      XEvent event = { .xclient = {
	  .type = ClientMessage, .window = window, .format = 32,
	  .message_type = XInternAtom (display, "WM_PROTOCOLS", False),
	  .data.l = { XInternAtom (display, "WM_DELETE_WINDOW", False),
		      CurrentTime } } };
      XSendEvent (display, window, False, NoEventMask, &event);
      XSync (display, False);
    }
  XCloseDisplay (display);
  return window != 0;
}

/* Wait at most CONNECT_LIMIT milliseconds for the window of the
   program PID to show.  Return the window, or 0.  */
static Window
wait_for_window (Display *display, pid_t pid)
{
  Window window;
  int64_t end = now_ms () + CONNECT_LIMIT;
  while (!(window = find_window (display, DefaultRootWindow (display),
				pid))
	 && now_ms () < end)
    poll (NULL, 0, 20);
  return window;
}

/* Return the window of the program PID: a child of ROOT that shows,
   has the _NET_WM_PID PID, and has the WM_DELETE_WINDOW protocol.
   Return 0 if there is none.  The test runs in Xvfb with no window
   manager, so the window is a child of ROOT.  */
static Window
find_window (Display *display, Window root, pid_t pid)
{
  Window parent, *children, found = 0;
  unsigned int count;
  if (!XQueryTree (display, root, &root, &parent, &children, &count))
    return 0;
  Atom delete = XInternAtom (display, "WM_DELETE_WINDOW", False);
  for (unsigned int i = 0; !found && i < count; i++)
    {
      XWindowAttributes attributes;
      Atom *protocols;
      int nprotocols;
      if (XGetWindowAttributes (display, children[i], &attributes)
	  && attributes.map_state == IsViewable
	  && window_pid (display, children[i]) == pid
	  && XGetWMProtocols (display, children[i], &protocols, &nprotocols))
	{
	  for (int j = 0; j < nprotocols; j++)
	    if (protocols[j] == delete)
	      found = children[i];
	  XFree (protocols);
	}
    }
  XFree (children);
  return found;
}

/* Return the _NET_WM_PID of WINDOW, or -1 if it has none.  */
static pid_t
window_pid (Display *display, Window window)
{
  Atom type;
  int format;
  unsigned long count, after;
  unsigned char *data = NULL;
  pid_t pid = -1;
  if (XGetWindowProperty (display, window,
			  XInternAtom (display, "_NET_WM_PID", False),
			  0, 1, False, XA_CARDINAL, &type, &format, &count,
			  &after, &data) == Success
      && data && type == XA_CARDINAL && format == 32 && count == 1)
    pid = *(unsigned long *) data;
  if (data)
    XFree (data);
  return pid;
}

/* Return the time of a monotonic clock in milliseconds.  */
static int64_t
now_ms (void)
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
