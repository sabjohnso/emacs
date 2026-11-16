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
   view must send.  The menu examples also show a popup menu, and do to
   it what a user, the server, or the window system does.

   The examples of child frames use the root frame 1 of 300 by 200 and
   its child frame 2 of CHILD_WIDTH by CHILD_HEIGHT at CHILD_X,
   CHILD_Y.  The child has no window of its own.  The input in the
   area of the child goes to the child, in the coordinates of the
   child.  As with the implicit grab of X, the input after a press in
   the child goes to the child until the release of the last button,
   also outside the child.

   The menu law holds for each random menu of menugen.h, with a random
   title.  The labels, keys and titles have random bytes, or UTF-8 that
   the end can cut in a character.
   - Widgets: the menu has the title item, if the title is not empty,
     and then one item for each entry, in order.  A submenu-start
     entry gives an item with a submenu that has the items of the
     entries up to its submenu-end entry.  Each item is sensitive if
     and only if its entry is enabled.  A separator gives a separator
     item.  A toggle or a radio item gives a check item, drawn as a
     radio for a radio, and active if and only if it is selected.
     Each label shows the valid UTF-8 of its bytes (U+FFFD for each
     bad byte), up to the first NUL byte.  The key shows in a second
     label, if it is not empty.
   - Choice: the activation of a random enabled item sends exactly one
     menu choice, with the id of that item.

   The program runs TRIALS trials of the law from a seed that it gets
   from the clock, and writes the seed of a failed trial.  With one
   argument, it runs the trial of that seed only.  */

#include <config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "menu.h"
#include "menugen.h"
#include "view.h"

/* The longest time that an example waits for a message, in
   microseconds.  */
enum { WAIT_LIMIT = 3000000 };

/* The place and the size of the child frame of the examples.  */
enum { CHILD = 2, CHILD_X = 50, CHILD_Y = 40, CHILD_WIDTH = 60,
       CHILD_HEIGHT = 30 };

/* The number of trials of the menu law.  */
enum { TRIALS = 40 };

/* The enabled items of a menu: the widget of each, and the id of its
   entry.  */
struct choices
{
  GtkWidget *items[MAX_MENU];
  uint32_t ids[MAX_MENU];
  int n;
};

/* The messages that a view sent, and the frames of the windows that
   the user asked to close.  */
enum { SENT_LIMIT = 4096 };
struct sent
{
  struct rd_message messages[SENT_LIMIT];
  int count;
  uint32_t closed[SENT_LIMIT];
  int closed_count;
};

/* One entry of a menu of an example.  */
struct menu_entry
{
  uint8_t kind;
  uint8_t flags;
  uint32_t id;
  const char *label;
  const char *key;
};

/* A menu with each kind of entry.  */
enum { ITEM = RD_MENU_KIND_ITEM, SEPARATOR = RD_MENU_KIND_SEPARATOR,
       START = RD_MENU_KIND_SUBMENU_START, END = RD_MENU_KIND_SUBMENU_END,
       ENABLED = RD_MENU_FLAG_ENABLED, TOGGLE = RD_MENU_FLAG_TOGGLE,
       RADIO = RD_MENU_FLAG_RADIO, SELECTED = RD_MENU_FLAG_SELECTED };
static const struct menu_entry file_menu[] =
  {
    { ITEM, ENABLED, 1, "Open", "C-x C-f" },
    { ITEM, ENABLED, 2, "Save", "" },
    { SEPARATOR, 0, 0, "", "" },
    { START, ENABLED, 0, "More", "" },
    { ITEM, ENABLED, 3, "Inner", "" },
    { END, 0, 0, "", "" },
    { ITEM, 0, 4, "Disabled", "" },
    { ITEM, ENABLED | TOGGLE | SELECTED, 5, "Toggle on", "" },
    { ITEM, ENABLED | TOGGLE, 6, "Toggle off", "" },
    { ITEM, ENABLED | RADIO | SELECTED, 7, "Radio", "" },
    { ITEM, ENABLED, 8, "Bad \xff text", "" },
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
static bool status_example (void);
static bool count_example (void);
static bool close_example (void);
static bool child_window_example (void);
static bool child_input_example (void);
static bool grab_example (void);
static bool hide_root_example (void);
static bool forget_child_example (void);
static struct example *start_child_example (void);
static void place_frame (struct example *, uint32_t, uint32_t, int32_t,
			 int32_t, bool);
static void define_any_frame (struct example *, uint32_t, uint32_t,
			      uint32_t);
static void remove_any_frame (struct example *, uint32_t);
static int toplevel_count (void);
static void send_pointer (GdkEventType, int, int, guint, GdkModifierType);
static bool wait_for_pointer (struct example *, enum rd_type, uint32_t,
			      int, int, bool);
static bool choose_example (const char *, uint32_t, bool);
static bool dismiss_example (const char *, void (*) (GtkMenuShell *));
static bool hide_example (void);
static bool other_hide_example (void);
static bool close_menu_example (void);
static bool replace_example (void);
static bool no_window_example (void);
static bool bad_entries_example (void);
static bool remove_menu_frame_example (void);
static bool refused_grab_example (void);
static bool trigger_example (GdkEventType);
static bool widgets_example (void);
static bool menu_laws (void);
static bool menu_law (struct example *, uint64_t);
static void show_random_menu (struct example *, const struct menu *,
			      const uint8_t *, int);
static bool shows_menu (GtkMenuShell *, const struct menu *,
			const uint8_t *, int, struct choices *);
static bool shows_shell (GtkMenuShell *, const struct menu *, int *,
			 struct choices *);
static bool shows_items (GList *, const struct menu *, int *,
			 struct choices *);
static bool shows_entry (GtkWidget *, const struct menu *, int *,
			 struct choices *);
static bool shows_check (GtkWidget *, uint8_t);
static bool shows_texts (GtkWidget *, const uint8_t *, int, const uint8_t *,
			 int);
static bool label_shows (GtkWidget *, const uint8_t *, int);
static int choices_after (const struct sent *, int);
static void popdown (GtkMenuShell *);
static void show_menu (struct example *, uint32_t, uint32_t,
		       const struct menu_entry *, size_t, const char *);
static GtkMenuShell *wait_for_menu (void);
static GtkMenuShell *open_menu (void);
static GtkWidget *find_item (GtkMenuShell *, const char *);
static bool has_label_p (GtkWidget *, const char *);
static const char *first_label (GtkWidget *);
static bool wait_for_choice (struct example *, uint32_t, uint8_t, uint32_t);
static int choice_count (const struct sent *);
static void send_input (GdkEventType, guint32);
static void release_button (GtkMenuShell *, guint32);
static GdkWindow *grab_pointer (void);
static void ungrab_pointer (GdkWindow *);
static bool title_p (const char *);
static void remove_frame (struct example *);
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
static void record_close (void *, uint32_t);
static cairo_font_face_t *find_no_font (void *,
					  const struct rc_font_key *);

static const struct rc_font_source no_fonts = { find_no_font, NULL, NULL };

int
main (int argc, char **argv)
{
  if (!gtk_init_check (&argc, &argv))
    {
      fprintf (stderr, "test-view: no display; run it in xvfb-run\n");
      return 1;
    }
  /* A warning of GTK, such as a popup menu with no trigger event, is
     a failure.  */
  g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);
  if (argc == 2)
    {
      struct example *example = start_example (300, 200);
      bool passed = menu_law (example, strtoull (argv[1], NULL, 10));
      end_example (example);
      return passed ? 0 : 1;
    }
  bool passed = (show_example (300, 200) && show_example (640, 480)
		 && focus_example (true) && focus_example (false)
		 && resize_example (300, 200, 420, 260)
		 && resize_example (600, 400, 300, 200)
		 && define_example (300, 200, 500, 350)
		 && define_example (600, 400, 250, 180)
		 && drag_example () && status_example ()
		 && count_example () && close_example ()
		 && child_window_example () && child_input_example ()
		 && grab_example () && hide_root_example ()
		 && forget_child_example ()
		 && choose_example ("Save", 2, false)
		 && choose_example ("Save", 2, true)
		 && choose_example ("Inner", 3, true)
		 && dismiss_example ("cancel", gtk_menu_shell_cancel)
		 && dismiss_example ("popdown", popdown)
		 && hide_example () && other_hide_example ()
		 && close_menu_example ()
		 && replace_example () && no_window_example ()
		 && bad_entries_example () && remove_menu_frame_example ()
		 && refused_grab_example ()
		 && trigger_example (GDK_BUTTON_PRESS)
		 && trigger_example (GDK_KEY_PRESS)
		 && widgets_example () && menu_laws ());
  if (!passed)
    return 1;
  printf ("test-view: the examples and %d trials passed\n", TRIALS);
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
			       { record_close, record_message, NULL,
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

/* Free EXAMPLE.  The view must destroy its window and its menu, so
   that the next example has only its own window.  GTK can make one
   tooltip window for the display while a popup menu is open.  GTK
   keeps that window, and the view has no tooltips, so the check
   ignores it.  */
static void
end_example (struct example *example)
{
  rc_view_free (example->view);
  rc_renderer_free (example->renderer);
  g_free (example);
  while (g_main_context_iteration (NULL, false))
    ;
  GList *windows = gtk_window_list_toplevels ();
  int count = 0;
  for (GList *w = windows; w; w = w->next)
    count += strcmp (G_OBJECT_TYPE_NAME (w->data), "GtkTooltipWindow") != 0;
  g_list_free (windows);
  if (count > 0)
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

/* The client loses its connection, and then connects again.  The
   title of the window must show the loss, and the title must be
   "Emacs" again after the connect.  */
static bool
status_example (void)
{
  struct example *example = start_example (300, 200);
  bool passed = title_p ("Emacs");
  rc_view_set_connected (example->view, false);
  const char *lost = gtk_window_get_title (frame_window ());
  passed = (passed && lost && strstr (lost, "Emacs")
	    && strcmp (lost, "Emacs") != 0);
  rc_view_set_connected (example->view, true);
  passed = passed && title_p ("Emacs");
  if (!passed)
    fprintf (stderr, "test-view: the title does not show the status\n");
  end_example (example);
  return passed;
}

/* The server defines a frame, and then removes it.  The view must
   count the window of the frame while it exists.  */
static bool
count_example (void)
{
  struct example *example = start_example (300, 200);
  bool passed = rc_view_window_count (example->view) == 1;
  remove_frame (example);
  passed = passed && rc_view_window_count (example->view) == 0;
  if (!passed)
    fprintf (stderr, "test-view: the count of the windows is wrong\n");
  end_example (example);
  return passed;
}

/* The user asks to close the window of a frame, as with the close
   button of a window manager.  The view must give the frame to its
   on_close function once, and must keep the window, because the
   server decides whether to delete the frame.  */
static bool
close_example (void)
{
  struct example *example = start_example (300, 200);
  gtk_window_close (frame_window ());
  gint64 end = g_get_monotonic_time () + WAIT_LIMIT;
  while (example->sent.closed_count == 0 && g_get_monotonic_time () < end)
    if (!g_main_context_iteration (NULL, false))
      g_usleep (1000);
  run_for (WAIT_LIMIT / 10);
  bool passed = (example->sent.closed_count == 1
		 && example->sent.closed[0] == 1
		 && rc_view_window_count (example->view) == 1
		 && frame_window ()
		 && gtk_widget_get_mapped (GTK_WIDGET (frame_window ())));
  if (!passed)
    fprintf (stderr, "test-view: a close of a window is wrong\n");
  end_example (example);
  return passed;
}

/* The server places a child frame in the root frame, and defines it.
   The view must make no window for the child.  */
static bool
child_window_example (void)
{
  struct example *example = start_child_example ();
  run_for (WAIT_LIMIT / 10);
  bool passed = (rc_view_window_count (example->view) == 1
		 && toplevel_count () == 1);
  if (!passed)
    fprintf (stderr, "test-view: a child frame has a window\n");
  end_example (example);
  return passed;
}

/* The user presses a button in the area of the child, and then in the
   area of the root.  The view must send each press to the frame under
   the pointer, in the coordinates of that frame.  */
static bool
child_input_example (void)
{
  struct example *example = start_child_example ();
  send_pointer (GDK_BUTTON_PRESS, CHILD_X + 10, CHILD_Y + 5, 1, 0);
  bool passed = wait_for_pointer (example, RD_BUTTON, CHILD, 10, 5, true);
  send_pointer (GDK_BUTTON_RELEASE, CHILD_X + 10, CHILD_Y + 5, 1,
		GDK_BUTTON1_MASK);
  send_pointer (GDK_BUTTON_PRESS, 7, 9, 1, 0);
  passed = passed && wait_for_pointer (example, RD_BUTTON, 1, 7, 9, true);
  send_pointer (GDK_MOTION_NOTIFY, CHILD_X + 1, CHILD_Y + 2, 0,
		GDK_BUTTON1_MASK);
  passed = passed && wait_for_pointer (example, RD_MOTION, 1, CHILD_X + 1,
				       CHILD_Y + 2, false);
  if (!passed)
    fprintf (stderr, "test-view: the input of a child frame is wrong\n");
  end_example (example);
  return passed;
}

/* The user presses a button in the child, moves the pointer out of
   the child, and releases the button there.  The motion and the
   release must go to the child.  The motion after the release must go
   to the frame under the pointer.  */
static bool
grab_example (void)
{
  struct example *example = start_child_example ();
  send_pointer (GDK_BUTTON_PRESS, CHILD_X + 3, CHILD_Y + 4, 1, 0);
  send_pointer (GDK_MOTION_NOTIFY, 5, 6, 0, GDK_BUTTON1_MASK);
  bool passed = wait_for_pointer (example, RD_MOTION, CHILD, 5 - CHILD_X,
				  6 - CHILD_Y, false);
  send_pointer (GDK_BUTTON_RELEASE, 5, 6, 1, GDK_BUTTON1_MASK);
  passed = passed && wait_for_pointer (example, RD_BUTTON, CHILD,
				       5 - CHILD_X, 6 - CHILD_Y, false);
  send_pointer (GDK_MOTION_NOTIFY, 8, 9, 0, 0);
  passed = passed && wait_for_pointer (example, RD_MOTION, 1, 8, 9, false);
  if (!passed)
    fprintf (stderr, "test-view: the grab of a press in a child frame "
	     "is wrong\n");
  end_example (example);
  return passed;
}

/* The server hides the root frame, and then shows it again.  The
   window of the root must hide and show.  */
static bool
hide_root_example (void)
{
  struct example *example = start_example (300, 200);
  GtkWidget *window = GTK_WIDGET (frame_window ());
  place_frame (example, 1, 0, 0, 0, false);
  bool passed = !gtk_widget_get_visible (window);
  place_frame (example, 1, 0, 0, 0, true);
  passed = passed && gtk_widget_get_visible (window);
  if (!passed)
    fprintf (stderr, "test-view: the visibility of a root is wrong\n");
  end_example (example);
  return passed;
}

/* The server deletes the child frame, and later defines a frame with
   the same number and no place.  That frame is a root, so it must get
   a window.  */
static bool
forget_child_example (void)
{
  struct example *example = start_child_example ();
  remove_any_frame (example, CHILD);
  define_any_frame (example, CHILD, CHILD_WIDTH, CHILD_HEIGHT);
  bool passed = rc_view_window_count (example->view) == 2;
  if (!passed)
    fprintf (stderr, "test-view: a deleted child keeps its place\n");
  end_example (example);
  return passed;
}

/* Return a new example with the root frame 1 and its child frame
   CHILD.  As the server does, place the child before its first
   define-frame.  */
static struct example *
start_child_example (void)
{
  struct example *example = start_example (300, 200);
  place_frame (example, CHILD, 1, CHILD_X, CHILD_Y, true);
  define_any_frame (example, CHILD, CHILD_WIDTH, CHILD_HEIGHT);
  while (g_main_context_iteration (NULL, false))
    ;
  return example;
}

/* Give FRAME of EXAMPLE the parent PARENT, the position X, Y and the
   visibility VISIBLE, as a place-frame message does.  */
static void
place_frame (struct example *example, uint32_t frame, uint32_t parent,
	     int32_t x, int32_t y, bool visible)
{
  struct rd_place_frame place = { .frame = frame, .parent = parent,
				  .x = x, .y = y, .z = 1,
				  .visible = visible };
  rc_view_place (example->view, &place);
}

/* Define FRAME of EXAMPLE with the size WIDTH, HEIGHT.  */
static void
define_any_frame (struct example *example, uint32_t frame, uint32_t width,
		  uint32_t height)
{
  struct rd_message message =
    { .type = RD_DEFINE_FRAME,
      .u.define_frame = { .frame = frame, .width = width,
			  .height = height } };
  rc_view_update (example->view, rc_apply (example->renderer, &message));
}

/* Remove FRAME of EXAMPLE, as the server does.  */
static void
remove_any_frame (struct example *example, uint32_t frame)
{
  struct rd_message message =
    { .type = RD_DELETE_FRAME, .u.delete_frame = { .frame = frame } };
  rc_view_update (example->view, rc_apply (example->renderer, &message));
}

/* Return the number of the windows of GTK.  */
static int
toplevel_count (void)
{
  GList *windows = gtk_window_list_toplevels ();
  int count = g_list_length (windows);
  g_list_free (windows);
  return count;
}

/* Give the drawing area of the window of the example a pointer event
   of TYPE at X, Y, with BUTTON and the modifier state STATE before the
   event, as X does.  */
static void
send_pointer (GdkEventType type, int x, int y, guint button,
	      GdkModifierType state)
{
  GtkWidget *area = gtk_bin_get_child (GTK_BIN (frame_window ()));
  GdkEvent *event = gdk_event_new (type);
  GdkDevice *pointer
    = gdk_seat_get_pointer (gdk_display_get_default_seat
			    (gtk_widget_get_display (area)));
  gdk_event_set_device (event, pointer);
  if (type == GDK_MOTION_NOTIFY)
    {
      event->motion.window = g_object_ref (gtk_widget_get_window (area));
      event->motion.x = x;
      event->motion.y = y;
      event->motion.state = state;
    }
  else
    {
      event->button.window = g_object_ref (gtk_widget_get_window (area));
      event->button.x = x;
      event->button.y = y;
      event->button.button = button;
      event->button.state = state;
    }
  gtk_widget_event (area, event);
  gdk_event_free (event);
}

/* Run the main loop until the view of EXAMPLE sent a message of TYPE
   (RD_BUTTON or RD_MOTION) for FRAME at X, Y, or until WAIT_LIMIT.
   For RD_BUTTON, PRESS tells a press from a release.  Return true if
   the view sent it.  */
static bool
wait_for_pointer (struct example *example, enum rd_type type,
		  uint32_t frame, int x, int y, bool press)
{
  struct rd_message expected = { .type = type };
  if (type == RD_BUTTON)
    expected.u.button = (struct rd_button) { .frame = frame, .x = x,
					     .y = y, .press = press };
  else
    expected.u.motion = (struct rd_motion) { .frame = frame, .x = x,
					     .y = y };
  return wait_for_message (example, &expected);
}

/* The view shows a menu, and the user selects the item LABEL with
   the id ID.  If SHELL, the selection comes as from a click: the menu
   shell first closes the menu and then activates the item.  The view
   must send one choice of ID, and close the menu.  */
static bool
choose_example (const char *label, uint32_t id, bool shell)
{
  struct example *example = start_example (300, 200);
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  GtkMenuShell *menu = wait_for_menu ();
  GtkWidget *item = menu ? find_item (menu, label) : NULL;
  if (item && shell)
    gtk_menu_shell_activate_item (GTK_MENU_SHELL
				  (gtk_widget_get_parent (item)),
				  item, true);
  else if (item)
    gtk_widget_activate (item);
  bool passed = item && wait_for_choice (example, 5, 1, id);
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 1 && !open_menu ();
  if (!passed)
    fprintf (stderr, "test-view: a choice of %s (shell %d) is wrong\n",
	     label, shell);
  end_example (example);
  return passed;
}

/* The view shows a menu, and CLOSE closes it with no selection, as
   the user or the window system does.  The view must send one
   dismissal.  */
static bool
dismiss_example (const char *name, void (*close) (GtkMenuShell *))
{
  struct example *example = start_example (300, 200);
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  GtkMenuShell *menu = wait_for_menu ();
  if (menu)
    close (menu);
  bool passed = menu && wait_for_choice (example, 5, 0, 0);
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 1 && !open_menu ();
  if (!passed)
    fprintf (stderr, "test-view: a %s of a menu is wrong\n", name);
  end_example (example);
  return passed;
}

/* The server hides the menu that the view shows.  The view must close
   the menu and must send no choice.  */
static bool
hide_example (void)
{
  struct example *example = start_example (300, 200);
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  bool passed = wait_for_menu () != NULL;
  rc_view_hide_menu (example->view, 5);
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 0 && !open_menu ();
  if (!passed)
    fprintf (stderr, "test-view: a hide of a menu is wrong\n");
  end_example (example);
  return passed;
}

/* The connection of the menu ends.  The view must close the menu,
   and must send no choice.  */
static bool
close_menu_example (void)
{
  struct example *example = start_example (300, 200);
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  bool passed = wait_for_menu () != NULL;
  rc_view_close_menu (example->view);
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 0 && !open_menu ();
  if (!passed)
    fprintf (stderr, "test-view: the close of a menu is wrong\n");
  end_example (example);
  return passed;
}

/* The server hides a menu that is not open.  The view must keep the
   open menu, and send its choice later.  */
static bool
other_hide_example (void)
{
  struct example *example = start_example (300, 200);
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  bool passed = wait_for_menu () != NULL;
  rc_view_hide_menu (example->view, 4);
  run_for (WAIT_LIMIT / 10);
  GtkMenuShell *menu = open_menu ();
  passed = passed && menu && choice_count (&example->sent) == 0;
  if (menu)
    gtk_menu_shell_cancel (menu);
  passed = passed && wait_for_choice (example, 5, 0, 0);
  if (!passed)
    fprintf (stderr, "test-view: a hide of another menu is wrong\n");
  end_example (example);
  return passed;
}

/* The server shows a menu while another menu is open.  The view must
   close the old menu with no choice, and show the new menu.  */
static bool
replace_example (void)
{
  struct example *example = start_example (300, 200);
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  bool passed = wait_for_menu () != NULL;
  show_menu (example, 1, 6, file_menu, 2, "");
  GtkMenuShell *menu = wait_for_menu ();
  passed = passed && menu && !find_item (menu, "Disabled");
  if (menu)
    gtk_menu_shell_cancel (menu);
  passed = passed && wait_for_choice (example, 6, 0, 0);
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 1 && !open_menu ();
  if (!passed)
    fprintf (stderr, "test-view: a menu after a menu is wrong\n");
  end_example (example);
  return passed;
}

/* The server shows a menu for a frame with no window.  The view must
   send a dismissal at once.  */
static bool
no_window_example (void)
{
  struct example *example = start_example (300, 200);
  show_menu (example, 2, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  struct rd_message dismissal =
    { .type = RD_MENU_CHOICE, .u.menu_choice = { .serial = 5 } };
  bool passed = (sent_p (&example->sent, &dismissal)
		 && choice_count (&example->sent) == 1);
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 1 && !open_menu ();
  if (!passed)
    fprintf (stderr, "test-view: a menu with no window is wrong\n");
  end_example (example);
  return passed;
}

/* The server shows a menu whose entries do not nest correctly.  The
   view must send a dismissal at once.  */
static bool
bad_entries_example (void)
{
  static const struct menu_entry bad[] = { { END, 0, 0, "", "" } };
  struct example *example = start_example (300, 200);
  show_menu (example, 1, 5, bad, G_N_ELEMENTS (bad), "");
  struct rd_message dismissal =
    { .type = RD_MENU_CHOICE, .u.menu_choice = { .serial = 5 } };
  bool passed = (sent_p (&example->sent, &dismissal)
		 && choice_count (&example->sent) == 1);
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 1 && !open_menu ();
  if (!passed)
    fprintf (stderr, "test-view: a menu with bad entries is wrong\n");
  end_example (example);
  return passed;
}

/* The server removes the frame of the open menu.  The view must close
   the menu and send one dismissal.  */
static bool
remove_menu_frame_example (void)
{
  struct example *example = start_example (300, 200);
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  bool passed = wait_for_menu () != NULL;
  remove_frame (example);
  passed = passed && wait_for_choice (example, 5, 0, 0);
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 1 && !open_menu ();
  if (!passed)
    fprintf (stderr, "test-view: a menu of a removed frame is wrong\n");
  end_example (example);
  return passed;
}

/* Another program of the desktop has a grab of the pointer, and the
   server shows a menu.  The window system refuses the grab of the
   menu, as a compositor can, so GTK does not show the menu.  The view
   must send one dismissal.  */
static bool
refused_grab_example (void)
{
  struct example *example = start_example (300, 200);
  GdkWindow *grab = grab_pointer ();
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  bool passed = grab && wait_for_choice (example, 5, 0, 0);
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 1 && !open_menu ();
  if (grab)
    ungrab_pointer (grab);
  if (!passed)
    fprintf (stderr, "test-view: a menu with a refused grab is wrong\n");
  end_example (example);
  return passed;
}

/* The window gets a press of the type TYPE, a key or a button, and
   then the server shows a menu.  The view must give the press to GTK
   as the trigger of the menu.  GTK then ignores a release of a button
   in the first 500 milliseconds after the press, so the click that
   opens a menu does not close it.  With the wrong trigger time, GTK
   closes the menu and the view sends a dismissal.  */
static bool
trigger_example (GdkEventType type)
{
  enum { PRESS_TIME = 100000 };
  struct example *example = start_example (300, 200);
  send_input (type, PRESS_TIME);
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "");
  GtkMenuShell *menu = wait_for_menu ();
  if (menu)
    release_button (menu, PRESS_TIME + 100);
  run_for (WAIT_LIMIT / 10);
  bool passed = (menu && open_menu () == menu
		 && choice_count (&example->sent) == 0);
  if (!passed)
    fprintf (stderr, "test-view: the trigger %d of a menu is wrong\n", type);
  end_example (example);
  return passed;
}

/* The view shows a menu with a title and each kind of entry.  The
   widgets of the menu must show the title, the flags, the key
   equivalents, and the submenus of the entries, and show a bad UTF-8
   label as valid UTF-8.  */
static bool
widgets_example (void)
{
  struct example *example = start_example (300, 200);
  show_menu (example, 1, 5, file_menu, G_N_ELEMENTS (file_menu), "Files");
  GtkMenuShell *menu = wait_for_menu ();
  bool passed = menu != NULL;
  if (menu)
    {
      GList *children = gtk_container_get_children (GTK_CONTAINER (menu));
      GtkWidget *title = children ? children->data : NULL;
      passed = (title && has_label_p (title, "Files")
		&& !gtk_widget_get_sensitive (title));
      g_list_free (children);
      GtkWidget *open = find_item (menu, "Open");
      GtkWidget *more = find_item (menu, "More");
      GtkWidget *on = find_item (menu, "Toggle on");
      GtkWidget *off = find_item (menu, "Toggle off");
      GtkWidget *radio = find_item (menu, "Radio");
      GtkWidget *bad = find_item (menu, "Bad \357\277\275 text");
      passed = (passed && open && has_label_p (open, "C-x C-f")
		&& gtk_widget_get_sensitive (open)
		&& !gtk_widget_get_sensitive (find_item (menu, "Disabled"))
		&& more && gtk_menu_item_get_submenu (GTK_MENU_ITEM (more))
		&& find_item (menu, "Inner")
		&& GTK_IS_CHECK_MENU_ITEM (on)
		&& gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (on))
		&& GTK_IS_CHECK_MENU_ITEM (off)
		&& !gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (off))
		&& GTK_IS_CHECK_MENU_ITEM (radio)
		&& gtk_check_menu_item_get_draw_as_radio
		     (GTK_CHECK_MENU_ITEM (radio))
		&& !GTK_IS_CHECK_MENU_ITEM (open) && bad);
    }
  run_for (WAIT_LIMIT / 10);
  passed = passed && choice_count (&example->sent) == 0;
  if (!passed)
    fprintf (stderr, "test-view: the widgets of a menu are wrong\n");
  end_example (example);
  return passed;
}

/* Run TRIALS trials of the menu law in one frame, from a seed that
   comes from the clock.  Write the seed of a failed trial.  */
static bool
menu_laws (void)
{
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  struct example *example = start_example (300, 200);
  bool passed = true;
  for (int trial = 0; passed && trial < TRIALS; trial++)
    {
      passed = menu_law (example, base + trial);
      if (!passed)
	fprintf (stderr, "test-view: the menu law failed with seed %llu\n",
		 (unsigned long long) (base + trial));
    }
  end_example (example);
  return passed;
}

/* Run the trial SEED of the menu law in the frame of EXAMPLE.  Return
   true if the law holds.  */
static bool
menu_law (struct example *example, uint64_t seed)
{
  static struct menu menu;
  uint64_t state = seed;
  uint32_t serial = random_next (&state);
  random_menu (&state, serial, &menu);
  uint8_t title[RD_MAX_MENU_TEXT];
  int ntitle = (random_below (&state, 2)
		? random_below (&state, RD_MAX_MENU_TEXT + 1) : 0);
  random_text (&state, title, ntitle);
  int first = example->sent.count;
  show_random_menu (example, &menu, title, ntitle);
  GtkMenuShell *shell = wait_for_menu ();
  static struct choices choices;
  choices.n = 0;
  bool passed = shell && shows_menu (shell, &menu, title, ntitle, &choices);
  if (passed && choices.n > 0)
    {
      int chosen = random_below (&state, choices.n);
      gtk_menu_item_activate (GTK_MENU_ITEM (choices.items[chosen]));
      passed = wait_for_choice (example, serial, 1, choices.ids[chosen]);
      run_for (WAIT_LIMIT / 30);
      passed = passed && choices_after (&example->sent, first) == 1;
    }
  rc_view_close_menu (example->view);
  return passed;
}

/* Give the entries of MENU, with the NTITLE bytes TITLE, to the view of
   EXAMPLE at the position 10, 20 of the frame 1, as the client
   does.  */
static void
show_random_menu (struct example *example, const struct menu *menu,
		  const uint8_t *title, int ntitle)
{
  uint32_t serial = menu->entries[0].serial;
  struct rc_menu_model model = { 0 };
  for (int i = 0; i < menu->n; i++)
    rc_menu_add (&model, &menu->entries[i]);
  struct rd_show_menu show =
    { .frame = 1, .serial = serial, .x = 10, .y = 20, .ntitle = ntitle };
  memcpy (show.title, title, ntitle);
  struct rc_menu *tree = rc_menu_show (&model, serial);
  rc_view_show_menu (example->view, &show, tree);
  rc_menu_free (tree);
  rc_menu_model_free (&model);
}

/* Return true if SHELL shows the NTITLE bytes TITLE and the entries of
   MENU, as the widgets part of the menu law says.  Add the enabled
   items to CHOICES.  */
static bool
shows_menu (GtkMenuShell *shell, const struct menu *menu,
	    const uint8_t *title, int ntitle, struct choices *choices)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (shell));
  GList *items = children;
  bool passed = true;
  if (ntitle > 0)
    {
      passed = (items && !gtk_widget_get_sensitive (items->data)
		&& shows_texts (items->data, title, ntitle, NULL, 0));
      items = items ? items->next : NULL;
    }
  int next = 0;
  passed = (passed && shows_items (items, menu, &next, choices)
	    && next == menu->n);
  g_list_free (children);
  return passed;
}

/* Return true if the items of SHELL show the entries of MENU from the
   entry *NEXT to the submenu-end entry that ends them.  Set *NEXT to
   the entry after that submenu-end entry.  Add the enabled items to
   CHOICES.  */
static bool
shows_shell (GtkMenuShell *shell, const struct menu *menu, int *next,
	     struct choices *choices)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (shell));
  bool passed = shows_items (children, menu, next, choices);
  g_list_free (children);
  return passed;
}

/* Return true if the widgets ITEMS show the entries of MENU from the
   entry *NEXT to the submenu-end entry that ends them, or to the end of
   MENU.  Set *NEXT to the entry after that end.  Add the enabled items
   to CHOICES.  */
static bool
shows_items (GList *items, const struct menu *menu, int *next,
	     struct choices *choices)
{
  GList *item = items;
  bool passed = true;
  while (passed && *next < menu->n
	 && menu->entries[*next].kind != RD_MENU_KIND_SUBMENU_END)
    {
      passed = item && shows_entry (item->data, menu, next, choices);
      item = item ? item->next : NULL;
    }
  if (*next < menu->n)
    (*next)++;
  return passed && !item;
}

/* Return true if ITEM shows the entry *NEXT of MENU.  Set *NEXT to the
   entry after it, or after the submenu that it starts.  Add an enabled
   item to CHOICES.  */
static bool
shows_entry (GtkWidget *item, const struct menu *menu, int *next,
	     struct choices *choices)
{
  const struct rd_menu_item *entry = &menu->entries[(*next)++];
  bool enabled = entry->flags & RD_MENU_FLAG_ENABLED;
  GtkWidget *submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (item));
  if (gtk_widget_get_sensitive (item) != enabled)
    return false;
  switch (entry->kind)
    {
    case RD_MENU_KIND_SEPARATOR:
      return GTK_IS_SEPARATOR_MENU_ITEM (item);
    case RD_MENU_KIND_SUBMENU_START:
      return (submenu
	      && shows_texts (item, entry->label, entry->nlabel, entry->key,
			      entry->nkey)
	      && shows_shell (GTK_MENU_SHELL (submenu), menu, next, choices));
    default:
      if (enabled)
	{
	  choices->items[choices->n] = item;
	  choices->ids[choices->n++] = entry->id;
	}
      return (!submenu && shows_check (item, entry->flags)
	      && shows_texts (item, entry->label, entry->nlabel, entry->key,
			      entry->nkey));
    }
}

/* Return true if ITEM is a check item if and only if FLAGS has the
   toggle or the radio flag, and then shows the radio and the selected
   flags of FLAGS.  */
static bool
shows_check (GtkWidget *item, uint8_t flags)
{
  if (!(flags & (RD_MENU_FLAG_TOGGLE | RD_MENU_FLAG_RADIO)))
    return !GTK_IS_CHECK_MENU_ITEM (item);
  if (!GTK_IS_CHECK_MENU_ITEM (item))
    return false;
  GtkCheckMenuItem *check = GTK_CHECK_MENU_ITEM (item);
  return (gtk_check_menu_item_get_draw_as_radio (check)
	  == !!(flags & RD_MENU_FLAG_RADIO)
	  && gtk_check_menu_item_get_active (check)
	  == !!(flags & RD_MENU_FLAG_SELECTED));
}

/* Return true if ITEM shows the NLABEL bytes LABEL in a label, and then
   the NKEY bytes KEY in a second label if NKEY is not 0.  */
static bool
shows_texts (GtkWidget *item, const uint8_t *label, int nlabel,
	     const uint8_t *key, int nkey)
{
  GtkWidget *box = gtk_bin_get_child (GTK_BIN (item));
  if (!GTK_IS_CONTAINER (box))
    return false;
  GList *labels = gtk_container_get_children (GTK_CONTAINER (box));
  bool passed = (g_list_length (labels) == (nkey > 0 ? 2u : 1u)
		 && label_shows (labels->data, label, nlabel)
		 && (nkey == 0
		     || label_shows (labels->next->data, key, nkey)));
  g_list_free (labels);
  return passed;
}

/* Return true if WIDGET is a label whose text is valid UTF-8, and is
   the valid UTF-8 of the N bytes BYTES up to the first NUL byte.  */
static bool
label_shows (GtkWidget *widget, const uint8_t *bytes, int n)
{
  if (!GTK_IS_LABEL (widget))
    return false;
  const char *text = gtk_label_get_text (GTK_LABEL (widget));
  char *valid = g_utf8_make_valid ((const char *) bytes, n);
  bool passed = g_utf8_validate (text, -1, NULL) && !strcmp (text, valid);
  g_free (valid);
  return passed;
}

/* Close MENU as the window system does, with no menu-shell signal.  */
static void
popdown (GtkMenuShell *menu)
{
  gtk_menu_popdown (GTK_MENU (menu));
}

/* Give the N entries ENTRIES of the menu SERIAL, with the title TITLE,
   to the view of EXAMPLE at the position 10, 20 of FRAME, as the
   client does.  */
static void
show_menu (struct example *example, uint32_t frame, uint32_t serial,
	   const struct menu_entry *entries, size_t n, const char *title)
{
  struct rc_menu_model model = { 0 };
  for (size_t i = 0; i < n; i++)
    {
      struct rd_menu_item item =
	{ .serial = serial, .kind = entries[i].kind,
	  .flags = entries[i].flags, .id = entries[i].id,
	  .nlabel = strlen (entries[i].label),
	  .nkey = strlen (entries[i].key) };
      memcpy (item.label, entries[i].label, item.nlabel);
      memcpy (item.key, entries[i].key, item.nkey);
      rc_menu_add (&model, &item);
    }
  struct rd_show_menu show =
    { .frame = frame, .serial = serial, .x = 10, .y = 20,
      .ntitle = strlen (title) };
  memcpy (show.title, title, show.ntitle);
  struct rc_menu *menu = rc_menu_show (&model, serial);
  rc_view_show_menu (example->view, &show, menu);
  rc_menu_free (menu);
  rc_menu_model_free (&model);
}

/* Run the main loop until a menu is open, or until WAIT_LIMIT.
   Return the menu, or NULL.  */
static GtkMenuShell *
wait_for_menu (void)
{
  gint64 end = g_get_monotonic_time () + WAIT_LIMIT;
  while (!open_menu () && g_get_monotonic_time () < end)
    if (!g_main_context_iteration (NULL, false))
      g_usleep (1000);
  while (g_main_context_iteration (NULL, false))
    ;
  return open_menu ();
}

/* Return the visible popup menu that is not a submenu, or NULL.  */
static GtkMenuShell *
open_menu (void)
{
  GList *windows = gtk_window_list_toplevels ();
  GtkMenuShell *menu = NULL;
  for (GList *w = windows; w && !menu; w = w->next)
    {
      GtkWidget *child = gtk_bin_get_child (GTK_BIN (w->data));
      if (GTK_IS_MENU (child) && gtk_widget_get_visible (child)
	  && !gtk_menu_get_attach_widget (GTK_MENU (child)))
	menu = GTK_MENU_SHELL (child);
    }
  g_list_free (windows);
  return menu;
}

/* Return the item of SHELL or of its submenus whose first label is
   LABEL, or NULL.  */
static GtkWidget *
find_item (GtkMenuShell *shell, const char *label)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (shell));
  GtkWidget *found = NULL;
  for (GList *c = children; c && !found; c = c->next)
    {
      const char *text = first_label (c->data);
      GtkWidget *submenu
	= gtk_menu_item_get_submenu (GTK_MENU_ITEM (c->data));
      if (text && strcmp (text, label) == 0)
	found = c->data;
      else if (submenu)
	found = find_item (GTK_MENU_SHELL (submenu), label);
    }
  g_list_free (children);
  return found;
}

/* Return true if WIDGET is a label with the text TEXT, or has one.  */
static bool
has_label_p (GtkWidget *widget, const char *text)
{
  if (GTK_IS_LABEL (widget))
    return strcmp (gtk_label_get_text (GTK_LABEL (widget)), text) == 0;
  if (!GTK_IS_CONTAINER (widget))
    return false;
  GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
  bool found = false;
  for (GList *c = children; c && !found; c = c->next)
    found = has_label_p (c->data, text);
  g_list_free (children);
  return found;
}

/* Return the text of the first label of WIDGET, or NULL.  */
static const char *
first_label (GtkWidget *widget)
{
  if (GTK_IS_LABEL (widget))
    return gtk_label_get_text (GTK_LABEL (widget));
  if (!GTK_IS_CONTAINER (widget))
    return NULL;
  GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
  const char *text = NULL;
  for (GList *c = children; c && !text; c = c->next)
    text = first_label (c->data);
  g_list_free (children);
  return text;
}

/* Run the main loop until the view of EXAMPLE sent the menu choice
   SERIAL, CHOSEN, ID, or until WAIT_LIMIT.  Return true if the view
   sent it.  */
static bool
wait_for_choice (struct example *example, uint32_t serial, uint8_t chosen,
		 uint32_t id)
{
  struct rd_message expected =
    { .type = RD_MENU_CHOICE,
      .u.menu_choice = { .serial = serial, .chosen = chosen, .id = id } };
  return wait_for_message (example, &expected);
}

/* Return the number of menu choices in SENT.  */
static int
choice_count (const struct sent *sent)
{
  int count = 0;
  for (int i = 0; i < sent->count; i++)
    count += sent->messages[i].type == RD_MENU_CHOICE;
  return count;
}

/* Return the number of menu choices in SENT after the first FIRST
   messages.  */
static int
choices_after (const struct sent *sent, int first)
{
  int count = 0;
  for (int i = first; i < sent->count; i++)
    count += sent->messages[i].type == RD_MENU_CHOICE;
  return count;
}

/* Give the window of the example a press of the type TYPE, a key or
   a button, at the time TIME, as the window system does.  */
static void
send_input (GdkEventType type, guint32 time)
{
  GtkWidget *window = GTK_WIDGET (frame_window ());
  GtkWidget *target = (type == GDK_KEY_PRESS ? window
		       : gtk_bin_get_child (GTK_BIN (window)));
  GdkEvent *event = gdk_event_new (type);
  GdkSeat *seat = gdk_display_get_default_seat (gdk_display_get_default ());
  if (type == GDK_KEY_PRESS)
    {
      event->key.window = g_object_ref (gtk_widget_get_window (target));
      event->key.time = time;
      event->key.keyval = GDK_KEY_F10;
      gdk_event_set_device (event, gdk_seat_get_keyboard (seat));
    }
  else
    {
      event->button.window = g_object_ref (gtk_widget_get_window (target));
      event->button.time = time;
      event->button.button = 1;
      gdk_event_set_device (event, gdk_seat_get_pointer (seat));
    }
  gtk_widget_event (target, event);
  gdk_event_free (event);
}

/* Give MENU a release of the button 1 at the time TIME, outside its
   items, as the window system does.  */
static void
release_button (GtkMenuShell *menu, guint32 time)
{
  GdkEvent *event = gdk_event_new (GDK_BUTTON_RELEASE);
  GdkSeat *seat = gdk_display_get_default_seat (gdk_display_get_default ());
  event->button.window
    = g_object_ref (gtk_widget_get_window (GTK_WIDGET (menu)));
  event->button.time = time;
  event->button.button = 1;
  event->button.x = -100;
  event->button.y = -100;
  gdk_event_set_device (event, gdk_seat_get_pointer (seat));
  gtk_widget_event (GTK_WIDGET (menu), event);
  gdk_event_free (event);
}

/* Grab the pointer with a window of a new connection to the display,
   as another program does.  Return the window, or NULL if the grab
   failed.  */
static GdkWindow *
grab_pointer (void)
{
  GdkDisplay *display
    = gdk_display_open (gdk_display_get_name (gdk_display_get_default ()));
  if (!display)
    return NULL;
  GdkWindowAttr attributes = { .width = 10, .height = 10,
			       .wclass = GDK_INPUT_OUTPUT,
			       .window_type = GDK_WINDOW_TOPLEVEL };
  GdkWindow *window
    = gdk_window_new (gdk_screen_get_root_window
		      (gdk_display_get_default_screen (display)),
		      &attributes, 0);
  gdk_window_show (window);
  GdkGrabStatus status
    = gdk_seat_grab (gdk_display_get_default_seat (display), window,
		     GDK_SEAT_CAPABILITY_POINTER, false, NULL, NULL, NULL,
		     NULL);
  gdk_display_flush (display);
  if (status == GDK_GRAB_SUCCESS)
    return window;
  ungrab_pointer (window);
  return NULL;
}

/* Remove WINDOW of grab_pointer and its grab, and close its
   connection.  */
static void
ungrab_pointer (GdkWindow *window)
{
  GdkDisplay *display = gdk_window_get_display (window);
  gdk_window_destroy (window);
  gdk_display_close (display);
}

/* Return true if the window of the example has the title TITLE.  */
static bool
title_p (const char *title)
{
  const char *now = gtk_window_get_title (frame_window ());
  return now && strcmp (now, title) == 0;
}

/* Remove the frame of EXAMPLE, as the server does.  */
static void
remove_frame (struct example *example)
{
  struct rd_message message =
    { .type = RD_DELETE_FRAME, .u.delete_frame = { .frame = 1 } };
  rc_view_update (example->view, rc_apply (example->renderer, &message));
}

/* Return the window of the frame of the example, or NULL.  The
   example has only one window that is not a popup window.  */
static GtkWindow *
frame_window (void)
{
  GList *windows = gtk_window_list_toplevels ();
  GtkWindow *window = NULL;
  for (GList *w = windows; w && !window; w = w->next)
    if (gtk_window_get_window_type (w->data) == GTK_WINDOW_TOPLEVEL)
      window = w->data;
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

/* Return true if A and B are the same focus, resize, button, motion,
   or menu-choice message.  The time and the modifiers of a button or a motion do not
   count.  */
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
    case RD_BUTTON:
      return (a->u.button.frame == b->u.button.frame
	      && a->u.button.x == b->u.button.x
	      && a->u.button.y == b->u.button.y
	      && a->u.button.press == b->u.button.press);
    case RD_MOTION:
      return (a->u.motion.frame == b->u.motion.frame
	      && a->u.motion.x == b->u.motion.x
	      && a->u.motion.y == b->u.motion.y);
    case RD_MENU_CHOICE:
      return (a->u.menu_choice.serial == b->u.menu_choice.serial
	      && a->u.menu_choice.chosen == b->u.menu_choice.chosen
	      && a->u.menu_choice.id == b->u.menu_choice.id);
    default:
      /* An example waits only for these messages.  */
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

/* The on_close function of the view.  DATA is the struct sent.  */
static void
record_close (void *data, uint32_t frame)
{
  struct sent *sent = data;
  if (sent->closed_count < SENT_LIMIT)
    sent->closed[sent->closed_count++] = frame;
}

/* The find function of a font source with no fonts.  The examples
   draw no glyphs.  */
static cairo_font_face_t *
find_no_font (void *data, const struct rc_font_key *key)
{
  (void) data;
  (void) key;
  return NULL;
}
