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

#include <math.h>
#include <stdbool.h>

#include <gtk/gtk.h>

#include "compose.h"
#include "input.h"
#include "sizes.h"
#include "stack.h"
#include "table.h"
#include "view.h"

struct rc_view
{
  struct rc_renderer *renderer;
  struct rc_view_functions functions;
  /* The places of the frames.  */
  struct rc_stack *stack;
  /* The struct rc_window of each frame that has a window.  */
  struct rc_table windows;
  /* True if the client has a connection to its server.  */
  bool connected;
  /* The handler of the changes of the clipboard, or 0.  */
  gulong owner_handler;
  /* The number of texts that the view put on the clipboard.  */
  unsigned clipboard_sets;
  /* The popup menu that is open, or NULL.  */
  struct open_menu *menu;
};

/* The popup menu SERIAL of VIEW in the window of FRAME, and its GTK
   menu.  ANSWERED is true after the view sent the choice of the menu.
   CLOSE_SOURCE is the idle source that closes the menu, or 0.  */
struct open_menu
{
  struct rc_view *view;
  uint32_t frame;
  uint32_t serial;
  GtkWidget *menu;
  bool answered;
  guint close_source;
};

/* A request for the text of the clipboard by VIEW, after SETS texts
   that the view put on the clipboard.  The answer of GTK comes later,
   and an answer to a request before the last set of the view gives an
   old text, so the view ignores it.  */
struct clipboard_request
{
  struct rc_view *view;
  unsigned sets;
};

/* The window of the root frame FRAME of VIEW, the drawing area in
   it, and the sizes of the area.  If GRABBED, the pointer input goes
   to the frame GRAB until the release of the last button.  TRIGGER is
   a copy of the most recent key or button event of the window, or
   NULL.  A popup menu gives it to the window system as the cause of
   the menu, because a window system can refuse a menu with no recent
   input event.  */
struct rc_window
{
  struct rc_view *view;
  uint32_t frame;
  GtkWidget *window;
  GtkWidget *area;
  struct rc_sizes sizes;
  bool grabbed;
  uint32_t grab;
  GdkEvent *trigger;
};

/* The frame of a pointer event, and the point of the event in the
   coordinates of that frame.  */
struct target
{
  uint32_t frame;
  double x;
  double y;
};

static void show_frame (struct rc_view *, uint32_t);
static void remove_frame (struct rc_view *, uint32_t);
static void remove_window (struct rc_view *, uint32_t);
static void redraw_frame (struct rc_view *, uint32_t);
static void place_window (struct rc_view *, uint32_t);
static void refresh_window (uint32_t, void *, void *);
static void show_if_visible (struct rc_window *);
static struct rc_window *add_window (struct rc_view *, uint32_t,
				     struct rc_size);
static struct rc_size surface_size (cairo_surface_t *);
static void set_title (uint32_t, void *, void *);
static void destroy_window (void *);
static void clipboard_changed (GtkClipboard *, GdkEvent *, gpointer);
static void clipboard_text (GtkClipboard *, const gchar *, gpointer);
static gboolean draw_frame (GtkWidget *, cairo_t *, gpointer);
static gboolean close_requested (GtkWidget *, GdkEvent *, gpointer);
static gboolean key_pressed (GtkWidget *, GdkEventKey *, gpointer);
static gboolean button_changed (GtkWidget *, GdkEventButton *, gpointer);
static gboolean mouse_moved (GtkWidget *, GdkEventMotion *, gpointer);
static gboolean scrolled (GtkWidget *, GdkEventScroll *, gpointer);
static gboolean focus_changed (GtkWidget *, GdkEventFocus *, gpointer);
static void area_resized (GtkWidget *, GdkRectangle *, gpointer);
static void send (struct rc_window *, const struct rd_message *);
static struct target pointer_target (const struct rc_window *, double,
				     double);
static void note_button (struct rc_window *, const GdkEventButton *,
			 uint32_t);
static void note_trigger (struct rc_window *, GdkEvent *);
static struct open_menu *popup_menu (struct rc_window *,
				     const struct rd_show_menu *,
				     const struct rc_menu *);
static GdkEvent *menu_trigger (struct rc_window *);
static void add_entries (struct open_menu *, GtkMenuShell *,
			 const struct rc_menu_node *, size_t);
static GtkWidget *entry_item (struct open_menu *,
			      const struct rc_menu_node *);
static GtkWidget *choice_item (struct open_menu *,
			       const struct rc_menu_node *);
static GtkWidget *labelled (GtkWidget *, const char *, size_t,
			    const char *, size_t);
static GtkWidget *valid_label (const char *, size_t);
static void item_activated (GtkMenuItem *, gpointer);
static void menu_hidden (GtkWidget *, gpointer);
static void schedule_close (struct open_menu *);
static gboolean menu_closed (gpointer);
static void dismiss_menu (struct rc_view *);
static void close_menu (struct rc_view *);
static void answer (struct open_menu *, bool, uint32_t);
static void send_choice (struct rc_view *, uint32_t, bool, uint32_t);

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
  view->stack = rc_stack_new (rc_frame_size, renderer);
  if (functions.clipboard)
    view->owner_handler
      = g_signal_connect (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD),
			  "owner-change", G_CALLBACK (clipboard_changed),
			  view);
  return view;
}

void
rc_view_free (struct rc_view *view)
{
  close_menu (view);
  if (view->owner_handler)
    g_signal_handler_disconnect (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD),
				 view->owner_handler);
  rc_table_free (&view->windows, destroy_window);
  rc_stack_free (view->stack);
  g_free (view);
}

void
rc_view_set_clipboard (struct rc_view *view, const char *text, size_t size)
{
  view->clipboard_sets++;
  gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD), text,
			  size);
}

void
rc_view_request_clipboard (struct rc_view *view)
{
  if (!view->functions.clipboard)
    return;
  struct clipboard_request *request = g_new (struct clipboard_request, 1);
  *request = (struct clipboard_request) { view, view->clipboard_sets };
  gtk_clipboard_request_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD),
			      clipboard_text, request);
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
      remove_frame (view, change.frame);
      break;
    case RC_FRAME_DRAWN:
      redraw_frame (view, change.frame);
      break;
    case RC_NO_CHANGE:
      break;
    }
}

/* The windows that show the frame before and after the change are
   the windows of its old and its new root.  */
void
rc_view_place (struct rc_view *view, const struct rd_place_frame *place)
{
  redraw_frame (view, place->frame);
  rc_stack_place (view->stack, place);
  place_window (view, place->frame);
  redraw_frame (view, place->frame);
}

void
rc_view_reset_places (struct rc_view *view)
{
  rc_stack_reset (view->stack);
  rc_table_each (&view->windows, refresh_window, NULL);
}

void
rc_view_set_connected (struct rc_view *view, bool connected)
{
  view->connected = connected;
  rc_table_each (&view->windows, set_title, NULL);
}

void
rc_view_show_menu (struct rc_view *view, const struct rd_show_menu *show,
		   const struct rc_menu *menu)
{
  close_menu (view);
  struct rc_window *window = rc_table_get (&view->windows, show->frame);
  if (menu && window)
    view->menu = popup_menu (window, show, menu);
  else
    send_choice (view, show->serial, false, 0);
}

void
rc_view_hide_menu (struct rc_view *view, uint32_t serial)
{
  if (view->menu && view->menu->serial == serial)
    close_menu (view);
}

int
rc_view_window_count (const struct rc_view *view)
{
  return rc_table_count (&view->windows);
}

/* Make the window of the root FRAME, or give it the size of the
   frame.  The window does not take the size of an answer to a resize
   message, so that the user can change the size of the window.  A
   child frame has no window, so draw its root again.  */
static void
show_frame (struct rc_view *view, uint32_t frame)
{
  if (rc_stack_parent (view->stack, frame) != 0)
    {
      redraw_frame (view, frame);
      return;
    }
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

/* Forget FRAME, which the renderer removed.  Draw its root again, so
   that a child frame goes away from the window of its root.  */
static void
remove_frame (struct rc_view *view, uint32_t frame)
{
  redraw_frame (view, frame);
  rc_stack_forget (view->stack, frame);
  remove_window (view, frame);
}

/* Remove the window of FRAME, if it has one.  */
static void
remove_window (struct rc_view *view, uint32_t frame)
{
  if (view->menu && view->menu->frame == frame)
    dismiss_menu (view);
  struct rc_window *window = rc_table_remove (&view->windows, frame);
  if (window)
    destroy_window (window);
}

/* Draw again the window of the root of FRAME, if it has one.  */
static void
redraw_frame (struct rc_view *view, uint32_t frame)
{
  uint32_t root = rc_stack_root (view->stack, frame);
  struct rc_window *window = root ? rc_table_get (&view->windows, root)
    : NULL;
  if (window)
    gtk_widget_queue_draw (window->area);
}

/* Make the window of FRAME agree with its place: a child frame has no
   window, and a defined root has a window that shows only if the root
   is visible.  */
static void
place_window (struct rc_view *view, uint32_t frame)
{
  struct rc_window *window = rc_table_get (&view->windows, frame);
  if (rc_stack_parent (view->stack, frame) != 0)
    remove_window (view, frame);
  else if (window)
    show_if_visible (window);
  else if (rc_frame_surface (view->renderer, frame))
    show_frame (view, frame);
}

/* Make the window WINDOW of FRAME agree with its place, and draw it
   again.  DATA is not used.  This is a function of rc_table_each.  */
static void
refresh_window (uint32_t frame, void *window, void *data)
{
  (void) frame;
  (void) data;
  struct rc_window *w = window;
  show_if_visible (w);
  gtk_widget_queue_draw (w->area);
}

/* Show WINDOW if its root is visible, and hide it if not.  */
static void
show_if_visible (struct rc_window *window)
{
  gtk_widget_set_visible (window->window,
			  rc_stack_visible_p (window->view->stack,
					      window->frame));
}

/* Make and show a window of the size SIZE for FRAME, and return it.  */
static struct rc_window *
add_window (struct rc_view *view, uint32_t frame, struct rc_size size)
{
  struct rc_window *window = g_new (struct rc_window, 1);
  *window = (struct rc_window) { view, frame,
				 gtk_window_new (GTK_WINDOW_TOPLEVEL),
				 gtk_drawing_area_new (),
				 rc_sizes_new (size), false, 0, NULL };
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
  gtk_widget_show (window->area);
  show_if_visible (window);
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
  if (window->trigger)
    gdk_event_free (window->trigger);
  g_free (window);
}

/* The draw signal of the drawing area of a root frame.  DATA is the
   struct rc_window.  Paint the root and its visible children.  */
static gboolean
draw_frame (GtkWidget *area, cairo_t *cr, gpointer data)
{
  (void) area;
  struct rc_window *window = data;
  struct rc_view *view = window->view;
  rc_compose (cr, view->stack, window->frame, rc_frame_surface_of,
	      view->renderer);
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
  note_trigger (window, (GdkEvent *) event);
  if (event->is_modifier)
    return true;
  struct rd_message message = rc_key_message (window->frame, event->keyval,
					      event->state, event->time);
  send (window, &message);
  return true;
}

/* The button-press-event and button-release-event signals of the
   drawing area of a root frame.  DATA is the struct rc_window.  Send
   the button message of a press or a release to the target frame.
   GTK also gives a double and a triple press, which Emacs makes itself
   from the times.  */
static gboolean
button_changed (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  (void) widget;
  if (event->type != GDK_BUTTON_PRESS && event->type != GDK_BUTTON_RELEASE)
    return true;
  struct rc_window *window = data;
  note_trigger (window, (GdkEvent *) event);
  struct target target = pointer_target (window, event->x, event->y);
  note_button (window, event, target.frame);
  struct rd_message message
    = rc_button_message (target.frame, event->button, target.x, target.y,
			 event->state, event->time,
			 event->type == GDK_BUTTON_PRESS);
  send (window, &message);
  return true;
}

/* Start the grab of WINDOW at the first press of EVENT, in the frame
   FRAME, and stop it at the release of the last button.  The state of
   a release has the buttons before the release.  */
static void
note_button (struct rc_window *window, const GdkEventButton *event,
	     uint32_t frame)
{
  if (event->type == GDK_BUTTON_PRESS && !window->grabbed)
    {
      window->grabbed = true;
      window->grab = frame;
      return;
    }
  GdkModifierType buttons = (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK
			     | GDK_BUTTON3_MASK | GDK_BUTTON4_MASK
			     | GDK_BUTTON5_MASK);
  GdkModifierType released = (event->button >= 1 && event->button <= 5
			      ? GDK_BUTTON1_MASK << (event->button - 1)
			      : 0);
  if (event->type == GDK_BUTTON_RELEASE
      && (event->state & buttons & ~released) == 0)
    window->grabbed = false;
}

/* The motion-notify-event signal of the drawing area of a root frame.
   DATA is the struct rc_window.  Send the motion message to the
   target frame.  */
static gboolean
mouse_moved (GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  (void) widget;
  struct rc_window *window = data;
  struct target target = pointer_target (window, event->x, event->y);
  struct rd_message message = rc_motion_message (target.frame, target.x,
						 target.y, event->time);
  send (window, &message);
  return true;
}

/* Return the target of a pointer event at X, Y in WINDOW: the frame of
   the grab while it holds and its frame is shown, else the frame under
   the pointer, else the root.  */
static struct target
pointer_target (const struct rc_window *window, double x, double y)
{
  const struct rc_stack *stack = window->view->stack;
  int64_t origin_x, origin_y;
  if (window->grabbed
      && rc_stack_origin (stack, window->frame, window->grab, &origin_x,
			  &origin_y))
    return (struct target) { window->grab, x - origin_x, y - origin_y };
  int point_x = floor (x), point_y = floor (y);
  struct rc_hit hit;
  if (rc_stack_frame_at (stack, window->frame, point_x, point_y, &hit))
    return (struct target) { hit.frame, x - (point_x - hit.x),
			     y - (point_y - hit.y) };
  return (struct target) { window->frame, x, y };
}

/* The scroll-event signal of the drawing area of a root frame.  DATA
   is the struct rc_window.  Send a press of the wheel button of the
   scroll to the target frame, as in X.  */
static gboolean
scrolled (GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
  (void) widget;
  struct rc_window *window = data;
  int button = rc_scroll_button (event->direction, event->delta_x,
				 event->delta_y);
  if (button == 0)
    return true;
  struct target target = pointer_target (window, event->x, event->y);
  struct rd_message message
    = rc_button_message (target.frame, button, target.x, target.y,
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

/* Keep a copy of EVENT, a key or a button event of WINDOW, as the
   trigger of a popup menu.  */
static void
note_trigger (struct rc_window *window, GdkEvent *event)
{
  if (window->trigger)
    gdk_event_free (window->trigger);
  window->trigger = gdk_event_copy (event);
}

/* Make the GTK menu of the tree MENU of the show-menu message SHOW,
   and pop it up at the position of SHOW in WINDOW.  Return the open
   menu.  If the window system refuses the menu, GTK does not show the
   menu, or hides it at once.  Then the view sends a dismissal.  */
static struct open_menu *
popup_menu (struct rc_window *window, const struct rd_show_menu *show,
	    const struct rc_menu *menu)
{
  struct open_menu *open = g_new0 (struct open_menu, 1);
  *open = (struct open_menu) { window->view, show->frame, show->serial,
			       gtk_menu_new (), false, 0 };
  GtkMenuShell *shell = GTK_MENU_SHELL (open->menu);
  if (show->ntitle > 0)
    {
      GtkWidget *title = labelled (gtk_menu_item_new (),
				   (const char *) show->title, show->ntitle,
				   "", 0);
      gtk_widget_set_sensitive (title, false);
      gtk_menu_shell_append (shell, title);
    }
  add_entries (open, shell, menu->children, menu->nchildren);
  g_signal_connect (open->menu, "hide", G_CALLBACK (menu_hidden), open);
  gtk_widget_show_all (open->menu);
  GdkRectangle position = { show->x, show->y, 1, 1 };
  GdkEvent *trigger = menu_trigger (window);
  gtk_menu_popup_at_rect (GTK_MENU (open->menu),
			  gtk_widget_get_window (window->area), &position,
			  GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST,
			  trigger);
  gdk_event_free (trigger);
  if (!gtk_widget_get_visible (gtk_widget_get_toplevel (open->menu)))
    schedule_close (open);
  return open;
}

/* Return a new event that is the cause of a popup menu in WINDOW: a
   copy of the most recent key or button event of WINDOW.  If WINDOW
   has no such event, return a press of the pointer at the current
   time.  */
static GdkEvent *
menu_trigger (struct rc_window *window)
{
  if (window->trigger)
    return gdk_event_copy (window->trigger);
  GdkEvent *event = gdk_event_new (GDK_BUTTON_PRESS);
  GdkSeat *seat
    = gdk_display_get_default_seat (gtk_widget_get_display (window->area));
  event->button.window = g_object_ref (gtk_widget_get_window (window->area));
  event->button.time = GDK_CURRENT_TIME;
  event->button.button = 1;
  gdk_event_set_device (event, gdk_seat_get_pointer (seat));
  return event;
}

/* Append to SHELL the items of the N nodes NODES of the menu OPEN.  */
static void
add_entries (struct open_menu *open, GtkMenuShell *shell,
	     const struct rc_menu_node *nodes, size_t n)
{
  for (size_t i = 0; i < n; i++)
    gtk_menu_shell_append (shell, entry_item (open, &nodes[i]));
}

/* Return the menu item of NODE of the menu OPEN.  The item is
   insensitive if NODE is not enabled.  */
static GtkWidget *
entry_item (struct open_menu *open, const struct rc_menu_node *node)
{
  GtkWidget *item;
  switch (node->kind)
    {
    case RD_MENU_KIND_SEPARATOR:
      item = gtk_separator_menu_item_new ();
      break;
    case RD_MENU_KIND_SUBMENU_START:
      {
	GtkWidget *submenu = gtk_menu_new ();
	add_entries (open, GTK_MENU_SHELL (submenu), node->children,
		     node->nchildren);
	item = labelled (gtk_menu_item_new (), node->label, node->nlabel,
			 node->key, node->nkey);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
      }
      break;
    default:
      item = choice_item (open, node);
      break;
    }
  gtk_widget_set_sensitive (item, node->flags & RD_MENU_FLAG_ENABLED);
  return item;
}

/* Return the menu item of NODE, an item of the menu OPEN that the user
   can select.  A toggle or a radio item is a check menu item.  */
static GtkWidget *
choice_item (struct open_menu *open, const struct rc_menu_node *node)
{
  bool check = node->flags & (RD_MENU_FLAG_TOGGLE | RD_MENU_FLAG_RADIO);
  GtkWidget *item = check ? gtk_check_menu_item_new () : gtk_menu_item_new ();
  if (check)
    {
      GtkCheckMenuItem *c = GTK_CHECK_MENU_ITEM (item);
      gtk_check_menu_item_set_draw_as_radio (c, (node->flags
						 & RD_MENU_FLAG_RADIO));
      gtk_check_menu_item_set_active (c, node->flags & RD_MENU_FLAG_SELECTED);
    }
  g_object_set_data (G_OBJECT (item), "rc-menu-id",
		     GUINT_TO_POINTER (node->id));
  /* Connect after set_active, because set_active activates the
     item.  */
  g_signal_connect (item, "activate", G_CALLBACK (item_activated), open);
  return labelled (item, node->label, node->nlabel, node->key, node->nkey);
}

/* Put in ITEM the label of the NLABEL bytes LABEL, and at its right
   the key equivalent of the NKEY bytes KEY, if NKEY is not 0.  Return
   ITEM.  */
static GtkWidget *
labelled (GtkWidget *item, const char *label, size_t nlabel,
	  const char *key, size_t nkey)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *text = valid_label (label, nlabel);
  gtk_widget_set_hexpand (text, true);
  gtk_box_pack_start (GTK_BOX (box), text, true, true, 0);
  if (nkey > 0)
    {
      GtkWidget *accel = valid_label (key, nkey);
      gtk_style_context_add_class (gtk_widget_get_style_context (accel),
				   GTK_STYLE_CLASS_ACCELERATOR);
      gtk_box_pack_end (GTK_BOX (box), accel, false, false, 0);
    }
  gtk_container_add (GTK_CONTAINER (item), box);
  return item;
}

/* Return a label, at the left, of the N bytes TEXT.  Replace each
   byte that is not valid UTF-8 with U+FFFD.  */
static GtkWidget *
valid_label (const char *text, size_t n)
{
  char *valid = g_utf8_make_valid (text, n);
  GtkWidget *label = gtk_label_new (valid);
  g_free (valid);
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  return label;
}

/* The activate signal of a menu item.  DATA is the struct open_menu.
   Send the choice of the item, and close the menu.  GTK hides the
   menu before it activates the item, so the choice comes before the
   idle function of the hide.  */
static void
item_activated (GtkMenuItem *item, gpointer data)
{
  struct open_menu *open = data;
  answer (open, true,
	  GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (item),
					       "rc-menu-id")));
  schedule_close (open);
}

/* The hide signal of a menu.  DATA is the struct open_menu.  Close
   the menu later, because GTK can activate an item after the hide.  */
static void
menu_hidden (GtkWidget *menu, gpointer data)
{
  (void) menu;
  schedule_close (data);
}

/* Close the menu OPEN in an idle function, unless an idle function
   already closes it.  */
static void
schedule_close (struct open_menu *open)
{
  if (!open->close_source)
    open->close_source = g_idle_add (menu_closed, open);
}

/* The idle function that closes a menu.  DATA is the struct
   open_menu.  Send a dismissal if no item was selected, and close the
   menu.  */
static gboolean
menu_closed (gpointer data)
{
  struct open_menu *open = data;
  open->close_source = 0;
  dismiss_menu (open->view);
  return G_SOURCE_REMOVE;
}

/* Send the dismissal of the open menu of VIEW if the view did not
   send its choice, and close the menu.  */
static void
dismiss_menu (struct rc_view *view)
{
  answer (view->menu, false, 0);
  close_menu (view);
}

/* Close the open menu of VIEW, if it has one, with no choice.  */
static void
close_menu (struct rc_view *view)
{
  struct open_menu *open = view->menu;
  if (!open)
    return;
  view->menu = NULL;
  if (open->close_source)
    g_source_remove (open->close_source);
  g_signal_handlers_disconnect_by_data (open->menu, open);
  gtk_widget_destroy (open->menu);
  g_free (open);
}

/* Send the choice CHOSEN, ID of the menu OPEN, unless the view sent
   its choice.  */
static void
answer (struct open_menu *open, bool chosen, uint32_t id)
{
  if (open->answered)
    return;
  open->answered = true;
  send_choice (open->view, open->serial, chosen, id);
}

/* Send the menu-choice message SERIAL, CHOSEN, ID of VIEW.  */
static void
send_choice (struct rc_view *view, uint32_t serial, bool chosen, uint32_t id)
{
  struct rd_message message =
    { .type = RD_MENU_CHOICE,
      .u.menu_choice = { .serial = serial, .chosen = chosen,
			 .id = chosen ? id : 0 } };
  view->functions.send (view->functions.data, &message);
}

/* The function of the changes of the owner of the clipboard of the
   desktop.  DATA is the view.  Ask for the new text.  */
static void
clipboard_changed (GtkClipboard *clipboard, GdkEvent *event, gpointer data)
{
  (void) clipboard, (void) event;
  rc_view_request_clipboard (data);
}

/* The function of a request for the text of the clipboard.  DATA is the
   struct clipboard_request.  Give TEXT to the CLIPBOARD function of the
   view, if the view did not set the clipboard after the request.  The
   program runs no main loop after rc_view_free, so the view exists.  */
static void
clipboard_text (GtkClipboard *clipboard, const gchar *text, gpointer data)
{
  (void) clipboard;
  struct clipboard_request *request = data;
  struct rc_view *view = request->view;
  if (request->sets == view->clipboard_sets)
    view->functions.clipboard (view->functions.data, text);
  g_free (request);
}
