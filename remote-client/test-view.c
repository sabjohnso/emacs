/* The tests of the view of the remote display client.

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

/* The view needs a display.  The check of the Makefile runs this test
   in Xvfb, so no window comes on the screen of the user.  With no
   display, the test fails.

   Each example shows a frame in a view, does to its window what a
   user or a window manager does, and waits for the message that the
   view must send.  */

#include <config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include "view.h"

/* The longest time that an example waits for a message, in
   microseconds.  */
enum { WAIT_LIMIT = 3000000 };

/* The messages that a view sent.  */
enum { SENT_LIMIT = 4096 };
struct sent
{
  struct rd_message messages[SENT_LIMIT];
  int count;
};

/* A frame of an example: its view, and the messages of the view.  */
struct example
{
  struct rc_renderer *renderer;
  struct rc_view *view;
  struct sent sent;
};

static bool show_example (uint32_t, uint32_t);
static bool focus_example (bool);
static bool resize_example (uint32_t, uint32_t, int, int);
static bool define_example (uint32_t, uint32_t, uint32_t, uint32_t);
static bool wait_for_size (GtkWindow *, int, int);
static bool drag_example (void);
static void run_for (gint64);
static int sent_index (const struct sent *, uint32_t, uint32_t);
static struct example *start_example (uint32_t, uint32_t);
static void end_example (struct example *);
static void define_frame (struct example *, uint32_t, uint32_t);
static GtkWindow *frame_window (void);
static void change_focus (GtkWindow *, bool);
static bool wait_for_message (struct example *, const struct rd_message *);
static bool sent_p (const struct sent *, const struct rd_message *);
static bool same_message_p (const struct rd_message *,
			    const struct rd_message *);
static void record_message (void *, const struct rd_message *);
static void ignore_close (void *);
static cairo_font_face_t *find_no_font (void *, const uint8_t *, int,
					uint32_t);

static const struct rc_font_source no_fonts = { find_no_font, NULL, NULL };

int
main (int argc, char **argv)
{
  if (!gtk_init_check (&argc, &argv))
    {
      fprintf (stderr, "test-view: no display; run it in xvfb-run\n");
      return 1;
    }
  bool passed = (show_example (300, 200) && show_example (640, 480)
		 && focus_example (true) && focus_example (false)
		 && resize_example (300, 200, 420, 260)
		 && resize_example (600, 400, 300, 200)
		 && define_example (300, 200, 500, 350)
		 && define_example (600, 400, 250, 180)
		 && drag_example ());
  if (!passed)
    return 1;
  printf ("test-view: the examples passed\n");
  return 0;
}

/* The server defines a new frame of WIDTH, HEIGHT.  Its window must
   get that size, and must send no resize message.  */
static bool
show_example (uint32_t width, uint32_t height)
{
  struct example *example = start_example (width, height);
  bool passed = wait_for_size (frame_window (), width, height);
  run_for (WAIT_LIMIT / 10);
  for (int i = 0; i < example->sent.count; i++)
    if (example->sent.messages[i].type == RD_RESIZE)
      passed = false;
  if (!passed)
    fprintf (stderr, "test-view: the new window of %ux%u is wrong\n",
	     width, height);
  end_example (example);
  return passed;
}

/* The window of a frame gets (IN is true) or loses the focus.  The
   view must send the focus message.  */
static bool
focus_example (bool in)
{
  struct example *example = start_example (300, 200);
  change_focus (frame_window (), in);
  struct rd_message expected =
    { .type = RD_FOCUS, .u.focus = { .frame = 1, .in = in } };
  bool passed = wait_for_message (example, &expected);
  if (!passed)
    fprintf (stderr, "test-view: no focus message (in %d)\n", in);
  end_example (example);
  return passed;
}

/* The server defines a frame of WIDTH, HEIGHT, and the user gives its
   window the size NEW_WIDTH, NEW_HEIGHT.  The view must send the resize
   message of the new size.  */
static bool
resize_example (uint32_t width, uint32_t height, int new_width,
		int new_height)
{
  struct example *example = start_example (width, height);
  gtk_window_resize (frame_window (), new_width, new_height);
  struct rd_message expected =
    { .type = RD_RESIZE,
      .u.resize = { .frame = 1, .width = new_width,
		    .height = new_height } };
  bool passed = wait_for_message (example, &expected);
  if (!passed)
    fprintf (stderr, "test-view: no resize message from %ux%u to %dx%d\n",
	     width, height, new_width, new_height);
  end_example (example);
  return passed;
}

/* The server defines a frame of WIDTH, HEIGHT, and then defines it
   again with the size NEW_WIDTH, NEW_HEIGHT.  The window must get the
   new size.  */
static bool
define_example (uint32_t width, uint32_t height, uint32_t new_width,
		uint32_t new_height)
{
  struct example *example = start_example (width, height);
  define_frame (example, new_width, new_height);
  bool passed = wait_for_size (frame_window (), new_width, new_height);
  if (!passed)
    fprintf (stderr, "test-view: the window did not get %ux%u\n",
	     new_width, new_height);
  run_for (WAIT_LIMIT / 10);
  struct rd_message echo =
    { .type = RD_RESIZE,
      .u.resize = { .frame = 1, .width = new_width,
		    .height = new_height } };
  if (passed && sent_p (&example->sent, &echo))
    {
      fprintf (stderr, "test-view: the size of the server came back\n");
      passed = false;
    }
  end_example (example);
  return passed;
}

/* The user changes the size of a window two times.  Then the answer to
   the first resize message comes.  The window must keep the size of
   the user.  */
static bool
drag_example (void)
{
  struct example *example = start_example (300, 200);
  GtkWindow *window = frame_window ();
  gtk_window_resize (window, 420, 260);
  bool passed = wait_for_size (window, 420, 260);
  gtk_window_resize (window, 500, 300);
  passed = passed && wait_for_size (window, 500, 300);
  int first = sent_index (&example->sent, 420, 260);
  int second = sent_index (&example->sent, 500, 300);
  passed = passed && first >= 0 && second > first;
  define_frame (example, 420, 260);
  run_for (WAIT_LIMIT / 10);
  passed = (passed && gtk_widget_get_allocated_width
	    (gtk_bin_get_child (GTK_BIN (window))) == 500);
  if (!passed)
    fprintf (stderr, "test-view: an answer changed the size of the user\n");
  end_example (example);
  return passed;
}

/* Return the index of the first resize message of WIDTH, HEIGHT in
   SENT, or -1 if SENT has none.  */
static int
sent_index (const struct sent *sent, uint32_t width, uint32_t height)
{
  for (int i = 0; i < sent->count; i++)
    if (sent->messages[i].type == RD_RESIZE
	&& sent->messages[i].u.resize.width == width
	&& sent->messages[i].u.resize.height == height)
      return i;
  return -1;
}

/* Run the main loop for TIME microseconds.  */
static void
run_for (gint64 time)
{
  gint64 end = g_get_monotonic_time () + time;
  while (g_get_monotonic_time () < end)
    if (!g_main_context_iteration (NULL, false))
      g_usleep (1000);
}

/* Run the main loop until the content of WINDOW has the size WIDTH,
   HEIGHT, or until WAIT_LIMIT.  Return true if it has the size.  */
static bool
wait_for_size (GtkWindow *window, int width, int height)
{
  GtkWidget *area = gtk_bin_get_child (GTK_BIN (window));
  gint64 end = g_get_monotonic_time () + WAIT_LIMIT;
  while ((gtk_widget_get_allocated_width (area) != width
	  || gtk_widget_get_allocated_height (area) != height)
	 && g_get_monotonic_time () < end)
    if (!g_main_context_iteration (NULL, false))
      g_usleep (1000);
  return (gtk_widget_get_allocated_width (area) == width
	  && gtk_widget_get_allocated_height (area) == height);
}

/* Return a new example with the frame 1 of WIDTH, HEIGHT in a view.
   Wait until GTK shows its window.  */
static struct example *
start_example (uint32_t width, uint32_t height)
{
  struct example *example = g_new0 (struct example, 1);
  example->renderer = rc_renderer_new (no_fonts);
  example->view = rc_view_new (example->renderer,
			       (struct rc_view_functions)
			       { ignore_close, record_message,
				 &example->sent });
  define_frame (example, width, height);
  gint64 end = g_get_monotonic_time () + WAIT_LIMIT;
  while (!gtk_widget_get_mapped (GTK_WIDGET (frame_window ()))
	 && g_get_monotonic_time () < end)
    g_main_context_iteration (NULL, false);
  while (g_main_context_iteration (NULL, false))
    ;
  return example;
}

/* Free EXAMPLE.  The view must destroy its window, so that the next
   example has only its own window.  */
static void
end_example (struct example *example)
{
  rc_view_free (example->view);
  rc_renderer_free (example->renderer);
  g_free (example);
  while (g_main_context_iteration (NULL, false))
    ;
  if (frame_window ())
    {
      fprintf (stderr, "test-view: a window stays after its view\n");
      abort ();
    }
}

/* Give the frame 1 of EXAMPLE the size WIDTH, HEIGHT.  */
static void
define_frame (struct example *example, uint32_t width, uint32_t height)
{
  struct rd_message message =
    { .type = RD_DEFINE_FRAME,
      .u.define_frame = { .frame = 1, .width = width, .height = height } };
  rc_view_update (example->view, rc_apply (example->renderer, &message));
}

/* Return the window of the frame of the example.  The example has only
   one window.  */
static GtkWindow *
frame_window (void)
{
  GList *windows = gtk_window_list_toplevels ();
  GtkWindow *window = windows ? windows->data : NULL;
  g_list_free (windows);
  return window;
}

/* Give the focus to WINDOW (IN is true) or take it from WINDOW, as a
   window manager does.  */
static void
change_focus (GtkWindow *window, bool in)
{
  GdkEvent *event = gdk_event_new (GDK_FOCUS_CHANGE);
  event->focus_change.window
    = g_object_ref (gtk_widget_get_window (GTK_WIDGET (window)));
  event->focus_change.send_event = true;
  event->focus_change.in = in;
  gtk_widget_send_focus_change (GTK_WIDGET (window), event);
  gdk_event_free (event);
}

/* Run the main loop until the view of EXAMPLE sent EXPECTED, or until
   WAIT_LIMIT.  Return true if the view sent it.  */
static bool
wait_for_message (struct example *example, const struct rd_message *expected)
{
  gint64 end = g_get_monotonic_time () + WAIT_LIMIT;
  while (!sent_p (&example->sent, expected)
	 && g_get_monotonic_time () < end)
    if (!g_main_context_iteration (NULL, false))
      g_usleep (1000);
  return sent_p (&example->sent, expected);
}

/* Return true if SENT has MESSAGE.  */
static bool
sent_p (const struct sent *sent, const struct rd_message *message)
{
  for (int i = 0; i < sent->count; i++)
    if (same_message_p (&sent->messages[i], message))
      return true;
  return false;
}

/* Return true if A and B are the same focus or resize message.  */
static bool
same_message_p (const struct rd_message *a, const struct rd_message *b)
{
  if (a->type != b->type)
    return false;
  switch (a->type)
    {
    case RD_FOCUS:
      return (a->u.focus.frame == b->u.focus.frame
	      && a->u.focus.in == b->u.focus.in);
    case RD_RESIZE:
      return (a->u.resize.frame == b->u.resize.frame
	      && a->u.resize.width == b->u.resize.width
	      && a->u.resize.height == b->u.resize.height);
    default:
      /* An example waits only for a focus or a resize message.  */
      abort ();
    }
}

/* The send function of the view.  DATA is the struct sent.  */
static void
record_message (void *data, const struct rd_message *message)
{
  struct sent *sent = data;
  if (sent->count < SENT_LIMIT)
    sent->messages[sent->count++] = *message;
}

/* The on_close function of the view.  No example closes a window.  */
static void
ignore_close (void *data)
{
  (void) data;
}

/* The find function of a font source with no fonts.  The examples
   draw no glyphs.  */
static cairo_font_face_t *
find_no_font (void *data, const uint8_t *hash, int nhash, uint32_t index)
{
  (void) data;
  (void) hash;
  (void) nhash;
  (void) index;
  return NULL;
}
