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
#include "table.h"
#include "view.h"

struct rc_view
{
  struct rc_renderer *renderer;
  struct rc_view_functions functions;
  /* The struct rc_window of each frame that has a window.  */
  struct rc_table windows;
};

/* The window of FRAME of VIEW, and the drawing area in it.  */
struct rc_window
{
  struct rc_view *view;
  uint32_t frame;
  GtkWidget *window;
  GtkWidget *area;
};

static void show_frame (struct rc_view *, uint32_t);
static void remove_window (struct rc_view *, uint32_t);
static void redraw_frame (struct rc_view *, uint32_t);
static struct rc_window *add_window (struct rc_view *, uint32_t);
static void destroy_window (void *);
static gboolean draw_frame (GtkWidget *, cairo_t *, gpointer);
static void window_closed (GtkWidget *, gpointer);
static gboolean key_pressed (GtkWidget *, GdkEventKey *, gpointer);
static gboolean button_changed (GtkWidget *, GdkEventButton *, gpointer);
static gboolean mouse_moved (GtkWidget *, GdkEventMotion *, gpointer);
static gboolean scrolled (GtkWidget *, GdkEventScroll *, gpointer);
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

/* Make the window of FRAME, or give it the size of the frame.  */
static void
show_frame (struct rc_view *view, uint32_t frame)
{
  cairo_surface_t *surface = rc_frame_surface (view->renderer, frame);
  struct rc_window *window = rc_table_get (&view->windows, frame);
  if (!window)
    window = add_window (view, frame);
  gtk_widget_set_size_request (window->area,
			       cairo_image_surface_get_width (surface),
			       cairo_image_surface_get_height (surface));
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

/* Make and show a window for FRAME, and return it.  */
static struct rc_window *
add_window (struct rc_view *view, uint32_t frame)
{
  struct rc_window *window = g_new (struct rc_window, 1);
  *window = (struct rc_window) { view, frame,
				 gtk_window_new (GTK_WINDOW_TOPLEVEL),
				 gtk_drawing_area_new () };
  g_signal_connect (window->area, "draw", G_CALLBACK (draw_frame), window);
  g_signal_connect (window->window, "destroy", G_CALLBACK (window_closed),
		    window);
  g_signal_connect (window->window, "key-press-event",
		    G_CALLBACK (key_pressed), window);
  gtk_widget_add_events (window->area, INPUT_EVENTS);
  for (size_t i = 0; i < G_N_ELEMENTS (input_signals); i++)
    g_signal_connect (window->area, input_signals[i].name,
		      input_signals[i].function, window);
  gtk_window_set_title (GTK_WINDOW (window->window), "Emacs");
  gtk_window_set_resizable (GTK_WINDOW (window->window), false);
  gtk_container_add (GTK_CONTAINER (window->window), window->area);
  gtk_widget_show_all (window->window);
  rc_table_set (&view->windows, frame, window);
  return window;
}

/* Destroy the GTK window of DATA, a struct rc_window that the view
   removed, and free DATA.  */
static void
destroy_window (void *data)
{
  struct rc_window *window = data;
  g_signal_handlers_disconnect_by_func (window->window, window_closed,
					window);
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

/* The destroy signal of a window that the user closed.  DATA is the
   struct rc_window.  Remove the window, and call the ON_CLOSE function
   of the view.  */
static void
window_closed (GtkWidget *widget, gpointer data)
{
  (void) widget;
  struct rc_window *window = data;
  struct rc_view *view = window->view;
  rc_table_remove (&view->windows, window->frame);
  g_free (window);
  view->functions.on_close (view->functions.data);
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

/* Give MESSAGE of the input of the user in WINDOW to the function of
   the view.  */
static void
send (struct rc_window *window, const struct rd_message *message)
{
  window->view->functions.send (window->view->functions.data, message);
}
