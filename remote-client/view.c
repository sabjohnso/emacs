/* The GTK view of the remote display client.

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

#include <stdbool.h>

#include <gtk/gtk.h>

#include "input.h"
#include "sizes.h"
#include "table.h"
#include "view.h"

struct rc_view
{
  struct rc_renderer *renderer;
  struct rc_view_functions functions;
  /* The struct rc_window of each frame that has a window.  */
  struct rc_table windows;
  /* True if the client has a connection to its server.  */
  bool connected;
};

/* The window of FRAME of VIEW, the drawing area in it, and the sizes
   of the area.  */
struct rc_window
{
  struct rc_view *view;
  uint32_t frame;
  GtkWidget *window;
  GtkWidget *area;
  struct rc_sizes sizes;
};

static void show_frame (struct rc_view *, uint32_t);
static void remove_window (struct rc_view *, uint32_t);
static void redraw_frame (struct rc_view *, uint32_t);
static struct rc_window *add_window (struct rc_view *, uint32_t,
				     struct rc_size);
static struct rc_size surface_size (cairo_surface_t *);
static void set_title (uint32_t, void *, void *);
static void destroy_window (void *);
static gboolean draw_frame (GtkWidget *, cairo_t *, gpointer);
static gboolean close_requested (GtkWidget *, GdkEvent *, gpointer);
static gboolean key_pressed (GtkWidget *, GdkEventKey *, gpointer);
static gboolean button_changed (GtkWidget *, GdkEventButton *, gpointer);
static gboolean mouse_moved (GtkWidget *, GdkEventMotion *, gpointer);
static gboolean scrolled (GtkWidget *, GdkEventScroll *, gpointer);
static gboolean focus_changed (GtkWidget *, GdkEventFocus *, gpointer);
static void area_resized (GtkWidget *, GdkRectangle *, gpointer);
static void send (struct rc_window *, const struct rd_message *);

/* The input signals of the drawing area of a frame, and their
   functions.  */
static const struct
{
  const char *name;
  GCallback function;
} input_signals[] =
  {
    { "button-press-event", G_CALLBACK (button_changed) },
    { "button-release-event", G_CALLBACK (button_changed) },
    { "motion-notify-event", G_CALLBACK (mouse_moved) },
    { "scroll-event", G_CALLBACK (scrolled) },
  };

/* The events of the input signals.  */
enum { INPUT_EVENTS = (GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
		       | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK
		       | GDK_SMOOTH_SCROLL_MASK) };

struct rc_view *
rc_view_new (struct rc_renderer *renderer,
	     struct rc_view_functions functions)
{
  struct rc_view *view = g_new0 (struct rc_view, 1);
  view->renderer = renderer;
  view->functions = functions;
  view->connected = true;
  return view;
}

void
rc_view_free (struct rc_view *view)
{
  rc_table_free (&view->windows, destroy_window);
  g_free (view);
}

void
rc_view_update (struct rc_view *view, struct rc_change change)
{
  switch (change.kind)
    {
    case RC_FRAME_SHOWN:
      show_frame (view, change.frame);
      break;
    case RC_FRAME_REMOVED:
      remove_window (view, change.frame);
      break;
    case RC_FRAME_DRAWN:
      redraw_frame (view, change.frame);
      break;
    case RC_NO_CHANGE:
      break;
    }
}

void
rc_view_set_connected (struct rc_view *view, bool connected)
{
  view->connected = connected;
  rc_table_each (&view->windows, set_title, NULL);
}

int
rc_view_window_count (const struct rc_view *view)
{
  return rc_table_count (&view->windows);
}

/* Make the window of FRAME, or give it the size of the frame.  The
   window does not take the size of an answer to a resize message, so
   that the user can change the size of the window.  */
static void
show_frame (struct rc_view *view, uint32_t frame)
{
  struct rc_size size
    = surface_size (rc_frame_surface (view->renderer, frame));
  struct rc_window *window = rc_table_get (&view->windows, frame);
  if (!window)
    window = add_window (view, frame, size);
  else if (rc_sizes_note_definition (&window->sizes, size))
    gtk_window_resize (GTK_WINDOW (window->window), size.width,
		       size.height);
  gtk_widget_queue_draw (window->area);
}

/* Remove the window of FRAME, if it has one.  */
static void
remove_window (struct rc_view *view, uint32_t frame)
{
  struct rc_window *window = rc_table_remove (&view->windows, frame);
  if (window)
    destroy_window (window);
}

/* Draw the window of FRAME again, if it has one.  */
static void
redraw_frame (struct rc_view *view, uint32_t frame)
{
  struct rc_window *window = rc_table_get (&view->windows, frame);
  if (window)
    gtk_widget_queue_draw (window->area);
}

/* Make and show a window of the size SIZE for FRAME, and return it.  */
static struct rc_window *
add_window (struct rc_view *view, uint32_t frame, struct rc_size size)
{
  struct rc_window *window = g_new (struct rc_window, 1);
  *window = (struct rc_window) { view, frame,
				 gtk_window_new (GTK_WINDOW_TOPLEVEL),
				 gtk_drawing_area_new (),
				 rc_sizes_new (size) };
  g_signal_connect (window->area, "draw", G_CALLBACK (draw_frame), window);
  g_signal_connect (window->window, "delete-event",
		    G_CALLBACK (close_requested), window);
  g_signal_connect (window->window, "key-press-event",
		    G_CALLBACK (key_pressed), window);
  g_signal_connect (window->window, "focus-in-event",
		    G_CALLBACK (focus_changed), window);
  g_signal_connect (window->window, "focus-out-event",
		    G_CALLBACK (focus_changed), window);
  gtk_widget_add_events (window->area, INPUT_EVENTS);
  for (size_t i = 0; i < G_N_ELEMENTS (input_signals); i++)
    g_signal_connect (window->area, input_signals[i].name,
		      input_signals[i].function, window);
  g_signal_connect (window->area, "size-allocate",
		    G_CALLBACK (area_resized), window);
  set_title (frame, window, NULL);
  gtk_container_add (GTK_CONTAINER (window->window), window->area);
  gtk_window_set_default_size (GTK_WINDOW (window->window), size.width,
			       size.height);
  gtk_widget_show_all (window->window);
  rc_table_set (&view->windows, frame, window);
  return window;
}

/* Give the window WINDOW of FRAME the title of the status of its
   view.  DATA is not used.  This is a function of rc_table_each.  */
static void
set_title (uint32_t frame, void *window, void *data)
{
  (void) frame;
  (void) data;
  struct rc_window *w = window;
  gtk_window_set_title (GTK_WINDOW (w->window),
			w->view->connected ? "Emacs" : "Emacs (reconnecting)");
}

/* Return the size of SURFACE.  */
static struct rc_size
surface_size (cairo_surface_t *surface)
{
  return (struct rc_size) { cairo_image_surface_get_width (surface),
			    cairo_image_surface_get_height (surface) };
}

/* Destroy the GTK window of DATA, a struct rc_window that the view
   removed, and free DATA.  */
static void
destroy_window (void *data)
{
  struct rc_window *window = data;
  gtk_widget_destroy (window->window);
  g_free (window);
}

/* The draw signal of the drawing area of a frame.  DATA is the struct
   rc_window.  Paint the surface of the frame.  */
static gboolean
draw_frame (GtkWidget *area, cairo_t *cr, gpointer data)
{
  (void) area;
  struct rc_window *window = data;
  cairo_surface_t *surface = rc_frame_surface (window->view->renderer,
					       window->frame);
  if (surface)
    {
      cairo_set_source_surface (cr, surface, 0, 0);
      cairo_paint (cr);
    }
  return true;
}

/* The delete-event signal of a window that the user asks to close.
   DATA is the struct rc_window.  Give its frame to the ON_CLOSE
   function of the view, and keep the window.  */
static gboolean
close_requested (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  (void) widget;
  (void) event;
  struct rc_window *window = data;
  struct rc_view *view = window->view;
  view->functions.on_close (view->functions.data, window->frame);
  return true;
}

/* The key-press-event signal of a window.  DATA is the struct
   rc_window.  Send the key message of the press, but not of a
   modifier key alone.  */
static gboolean
key_pressed (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  (void) widget;
  struct rc_window *window = data;
  if (event->is_modifier)
    return true;
  struct rd_message message = rc_key_message (window->frame, event->keyval,
					      event->state, event->time);
  send (window, &message);
  return true;
}

/* The button-press-event and button-release-event signals of the
   drawing area of a frame.  DATA is the struct rc_window.  Send the
   button message of a press or a release.  GTK also gives a double and
   a triple press, which Emacs makes itself from the times.  */
static gboolean
button_changed (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  (void) widget;
  if (event->type != GDK_BUTTON_PRESS && event->type != GDK_BUTTON_RELEASE)
    return true;
  struct rc_window *window = data;
  struct rd_message message
    = rc_button_message (window->frame, event->button, event->x, event->y,
			 event->state, event->time,
			 event->type == GDK_BUTTON_PRESS);
  send (window, &message);
  return true;
}

/* The motion-notify-event signal of the drawing area of a frame.  DATA
   is the struct rc_window.  Send the motion message.  */
static gboolean
mouse_moved (GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  (void) widget;
  struct rc_window *window = data;
  struct rd_message message = rc_motion_message (window->frame, event->x,
						 event->y, event->time);
  send (window, &message);
  return true;
}

/* The scroll-event signal of the drawing area of a frame.  DATA is the
   struct rc_window.  Send a press of the wheel button of the scroll, as
   in X.  */
static gboolean
scrolled (GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
  (void) widget;
  struct rc_window *window = data;
  int button = rc_scroll_button (event->direction, event->delta_x,
				 event->delta_y);
  if (button == 0)
    return true;
  struct rd_message message
    = rc_button_message (window->frame, button, event->x, event->y,
			 event->state, event->time, true);
  send (window, &message);
  return true;
}

/* The focus-in-event and focus-out-event signals of a window.  DATA
   is the struct rc_window.  Send the focus message.  GTK also handles
   the event, so that the window shows its focus.  */
static gboolean
focus_changed (GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
  (void) widget;
  struct rc_window *window = data;
  struct rd_message message = rc_focus_message (window->frame, event->in);
  send (window, &message);
  return false;
}

/* The size-allocate signal of the drawing area of a frame.  DATA is
   the struct rc_window.  Send the resize message of the new size of
   the area, if the server does not know the size.  */
static void
area_resized (GtkWidget *widget, GdkRectangle *allocation, gpointer data)
{
  (void) widget;
  struct rc_window *window = data;
  struct rc_size size = rc_size_of (allocation->width, allocation->height);
  if (!rc_sizes_note_allocation (&window->sizes, size))
    return;
  struct rd_message message = rc_resize_message (window->frame, size.width,
						 size.height);
  send (window, &message);
}

/* Give MESSAGE of the input of the user in WINDOW to the function of
   the view.  */
static void
send (struct rc_window *window, const struct rd_message *message)
{
  window->view->functions.send (window->view->functions.data, message);
}
