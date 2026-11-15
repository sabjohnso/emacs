/* The input from the client of a remote display session.

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

/* This file turns each input message of a client into an input event
   of Emacs.  It does not know the transport: remoteproc.c decodes the
   messages and gives them to remote_handle_input.  A message for a
   frame that the session does not have has no effect, because the
   frame can be deleted while the message comes.

   A key message has the X keysym of the key, as GTK gives it, and the
   Unicode character of the key.  As in pgtkterm.c, a keysym of a
   function key gives a NON_ASCII_KEYSTROKE_EVENT, and keyboard.c gives
   the name of the keysym.  Another key gives its character, if it has
   one.

   A button message gives a MOUSE_CLICK_EVENT, and keyboard.c makes
   the click, drag, and double click events from the presses and the
   releases.  The wheel buttons of rdproto.h are those of X: a press
   gives a wheel event, and a release gives none.  A motion message
   records the position of the mouse in the session, for the
   mouse_position_hook, and updates the mouse highlight.

   A focus message gives its frame the focus of the session, or takes
   the focus from it.  Emacs sees each change as a focus-out event of
   the old focus frame and a focus-in event of the new one.  A resize
   message changes the size of its frame at the next redisplay, and the
   redisplay sends the new size to the client.  A close-frame message
   gives the delete-frame event of its frame, as the WM_DELETE_WINDOW
   message of X does in xterm.c, so `handle-delete-frame' decides
   whether to delete the frame.  */

#include <config.h>

#include "lisp.h"
#include "character.h"
#include "dispextern.h"
#include "frame.h"
#include "keyboard.h"
#include "termhooks.h"
#include "remoteinput.h"

/* Keysyms with this bit are vendor keys.  */
enum { KEYSYM_VENDOR = 1 << 28 };

/* The largest width or height of a frame in pixels, as for a window of
   X.  A larger size from the client has no effect, because the glyph
   matrices of a frame grow with its size.  */
enum { REMOTE_FRAME_PIXEL_LIMIT = 32767 };

/* The wheel buttons, and the kind and the modifier of the event of
   each, as in pgtkterm.c.  */
struct remote_wheel
{
  uint8_t button;
  enum event_kind kind;
  int modifier;
};

static void remote_key_input (struct remote_display_info *,
			      const struct rd_message *);
static void remote_button_input (struct remote_display_info *,
				 const struct rd_message *);
static void remote_motion_input (struct remote_display_info *,
				 const struct rd_message *);
static void remote_focus_input (struct remote_display_info *,
				const struct rd_message *);
static void remote_resize_input (struct remote_display_info *,
				 const struct rd_message *);
static int remote_windows_min_size (struct frame *, Lisp_Object);
static void remote_close_input (struct remote_display_info *,
				const struct rd_message *);
static void remote_set_focus_frame (struct remote_display_info *,
				    struct frame *);
static void remote_focus_event (enum event_kind, struct frame *);
static bool remote_frame_pixels_p (uint32_t);
static void remote_wheel_event (struct frame *, const struct rd_button *,
				const struct remote_wheel *);
static void remote_click_event (struct frame *, const struct rd_button *);
static const struct remote_wheel *remote_wheel_of (uint8_t);
static void remote_clear_mouse_moved (struct remote_display_info *);
static void remote_set_position (struct input_event *, struct frame *,
				 int32_t, int32_t, uint32_t);
static bool remote_function_keysym_p (uint32_t);
static bool remote_modifier_keysym_p (uint32_t);
static int remote_emacs_modifiers (uint8_t);
static enum event_kind remote_character_kind (int);

/* The function of each type of input message.  A type with no function
   gives no event.  */
static void (*const remote_input_functions[RD_TYPE_LIMIT])
  (struct remote_display_info *, const struct rd_message *) =
  {
    [RD_KEY] = remote_key_input,
    [RD_BUTTON] = remote_button_input,
    [RD_MOTION] = remote_motion_input,
    [RD_FOCUS] = remote_focus_input,
    [RD_RESIZE] = remote_resize_input,
    [RD_CLOSE_FRAME] = remote_close_input,
    [RD_WANT_FONT] = remote_want_font,
    [RD_CLIPBOARD_DATA] = remote_clipboard_input,
  };

void
remote_handle_input (struct remote_display_info *dpyinfo,
		     const struct rd_message *message)
{
  if (remote_input_functions[message->type])
    remote_input_functions[message->type] (dpyinfo, message);
}

/* Store the key event of the key MESSAGE for its frame of DPYINFO.  */
static void
remote_key_input (struct remote_display_info *dpyinfo,
		  const struct rd_message *message)
{
  const struct rd_key *key = &message->u.key;
  struct frame *f = remote_frame_of_id (dpyinfo, key->frame);
  if (!f)
    return;
  struct input_event ie;
  EVENT_INIT (ie);
  XSETFRAME (ie.frame_or_window, f);
  ie.modifiers = remote_emacs_modifiers (key->modifiers);
  ie.timestamp = key->time;
  if (remote_function_keysym_p (key->keysym))
    {
      ie.kind = NON_ASCII_KEYSTROKE_EVENT;
      ie.code = key->keysym;
    }
  else if (key->character > 0 && key->character <= MAX_CHAR)
    {
      ie.kind = remote_character_kind (key->character);
      ie.code = key->character;
    }
  else
    return;
  kbd_buffer_store_event (&ie);
}

/* Store the click or the wheel event of the button MESSAGE for its
   frame of DPYINFO.  The button 0 is not a button.  */
static void
remote_button_input (struct remote_display_info *dpyinfo,
		     const struct rd_message *message)
{
  const struct rd_button *button = &message->u.button;
  struct frame *f = remote_frame_of_id (dpyinfo, button->frame);
  if (!f || button->button == 0)
    return;
  const struct remote_wheel *wheel = remote_wheel_of (button->button);
  if (!wheel)
    remote_click_event (f, button);
  else if (button->press)
    remote_wheel_event (f, button, wheel);
}

/* Record the mouse position of the motion MESSAGE in DPYINFO, and
   update the mouse highlight of its frame.  */
static void
remote_motion_input (struct remote_display_info *dpyinfo,
		     const struct rd_message *message)
{
  const struct rd_motion *motion = &message->u.motion;
  struct frame *f = remote_frame_of_id (dpyinfo, motion->frame);
  if (!f)
    return;
  dpyinfo->last_mouse_motion_frame = f;
  dpyinfo->last_mouse_motion_x = motion->x;
  dpyinfo->last_mouse_motion_y = motion->y;
  dpyinfo->last_mouse_movement_time = motion->time;
  f->mouse_moved = true;
  note_mouse_highlight (f, motion->x, motion->y);
}

/* Give the frame of the focus MESSAGE the focus of DPYINFO, or take
   the focus from it.  A focus-out message of a frame with no focus has
   no effect.  */
static void
remote_focus_input (struct remote_display_info *dpyinfo,
		    const struct rd_message *message)
{
  const struct rd_focus *focus = &message->u.focus;
  struct frame *f = remote_frame_of_id (dpyinfo, focus->frame);
  if (!f)
    return;
  if (focus->in)
    remote_set_focus_frame (dpyinfo, f);
  else if (dpyinfo->focus_frame == f)
    remote_set_focus_frame (dpyinfo, NULL);
}

/* Change the size of the frame of the resize MESSAGE of DPYINFO at the
   next redisplay.  A size of 0 or above REMOTE_FRAME_PIXEL_LIMIT has
   no effect.  A smaller size than the minimum size of the windows of
   the frame gives that minimum size, as the size hints of X do: a
   frame with no text column makes redisplay write past the end of its
   mode line buffer.  A remote frame has no internal border and no
   tool bar or tab bar height, so the windows fill the frame.  */
static void
remote_resize_input (struct remote_display_info *dpyinfo,
		     const struct rd_message *message)
{
  const struct rd_resize *resize = &message->u.resize;
  struct frame *f = remote_frame_of_id (dpyinfo, resize->frame);
  if (!f || !remote_frame_pixels_p (resize->width)
      || !remote_frame_pixels_p (resize->height))
    return;
  change_frame_size (f, max (resize->width,
			      remote_windows_min_size (f, Qt)),
		     max (resize->height,
			  remote_windows_min_size (f, Qnil)),
		     false, true, false);
}

/* Return the minimum size of the windows of F in pixels: the width if
   HORIZONTAL is non-nil, and else the height.  Return 0 if the value
   is not a fixnum.  window.el defines `frame-windows-min-size'; the C
   function of that name is a stub.  The filter of the connection gives
   the input, so Lisp can run.  */
static int
remote_windows_min_size (struct frame *f, Lisp_Object horizontal)
{
  Lisp_Object frame;
  XSETFRAME (frame, f);
  Lisp_Object size = calln (Qframe_windows_min_size, frame, horizontal,
			    Qnil, Qt);
  return RANGED_FIXNUMP (0, size, INT_MAX / 2) ? XFIXNUM (size) : 0;
}

/* Store the delete-frame event of the frame of the close-frame MESSAGE
   of DPYINFO.  */
static void
remote_close_input (struct remote_display_info *dpyinfo,
		    const struct rd_message *message)
{
  const struct rd_close_frame *close = &message->u.close_frame;
  struct frame *f = remote_frame_of_id (dpyinfo, close->frame);
  if (!f)
    return;
  struct input_event ie;
  EVENT_INIT (ie);
  ie.kind = DELETE_WINDOW_EVENT;
  XSETFRAME (ie.frame_or_window, f);
  ie.arg = Qnil;
  kbd_buffer_store_event (&ie);
}

void
remote_mouse_position (struct frame **fp, int insist,
		       Lisp_Object *bar_window, enum scroll_bar_part *part,
		       Lisp_Object *x, Lisp_Object *y, Time *timestamp)
{
  struct remote_display_info *dpyinfo = FRAME_DISPLAY_INFO (*fp);
  struct frame *f = dpyinfo->last_mouse_motion_frame;
  if (!f)
    return;
  remote_clear_mouse_moved (dpyinfo);
  *fp = f;
  *bar_window = Qnil;
  *part = scroll_bar_above_handle;
  XSETINT (*x, dpyinfo->last_mouse_motion_x);
  XSETINT (*y, dpyinfo->last_mouse_motion_y);
  *timestamp = dpyinfo->last_mouse_movement_time;
}

/* Give F the focus of DPYINFO, and store a focus-out event for the
   old focus frame and a focus-in event for F.  F is NULL to take the
   focus from each frame.  */
static void
remote_set_focus_frame (struct remote_display_info *dpyinfo,
			struct frame *f)
{
  struct frame *old = dpyinfo->focus_frame;
  if (f == old)
    return;
  remote_new_focus_frame (dpyinfo, f);
  if (old)
    remote_focus_event (FOCUS_OUT_EVENT, old);
  if (f)
    remote_focus_event (FOCUS_IN_EVENT, f);
}

/* Store a focus event of KIND for F.  */
static void
remote_focus_event (enum event_kind kind, struct frame *f)
{
  struct input_event ie;
  EVENT_INIT (ie);
  ie.kind = kind;
  XSETFRAME (ie.frame_or_window, f);
  ie.arg = Qnil;
  kbd_buffer_store_event (&ie);
}

/* Return true if PIXELS is a valid width or height of a frame.  */
static bool
remote_frame_pixels_p (uint32_t pixels)
{
  return pixels > 0 && pixels <= REMOTE_FRAME_PIXEL_LIMIT;
}

/* Store the WHEEL event of a press of BUTTON in F.  */
static void
remote_wheel_event (struct frame *f, const struct rd_button *button,
		    const struct remote_wheel *wheel)
{
  struct input_event ie;
  EVENT_INIT (ie);
  remote_set_position (&ie, f, button->x, button->y, button->time);
  ie.kind = wheel->kind;
  ie.modifiers = remote_emacs_modifiers (button->modifiers) | wheel->modifier;
  kbd_buffer_store_event (&ie);
}

/* Store the click event of a press or a release of BUTTON in F.  */
static void
remote_click_event (struct frame *f, const struct rd_button *button)
{
  struct input_event ie;
  EVENT_INIT (ie);
  remote_set_position (&ie, f, button->x, button->y, button->time);
  ie.kind = MOUSE_CLICK_EVENT;
  ie.code = button->button - 1;
  ie.modifiers = (remote_emacs_modifiers (button->modifiers)
		  | (button->press ? down_modifier : up_modifier));
  kbd_buffer_store_event (&ie);
}

/* The wheel buttons.  */
static const struct remote_wheel remote_wheels[] =
  {
    { RD_BUTTON_WHEEL_UP, WHEEL_EVENT, up_modifier },
    { RD_BUTTON_WHEEL_DOWN, WHEEL_EVENT, down_modifier },
    { RD_BUTTON_WHEEL_LEFT, HORIZ_WHEEL_EVENT, up_modifier },
    { RD_BUTTON_WHEEL_RIGHT, HORIZ_WHEEL_EVENT, down_modifier },
  };

/* Return the wheel of BUTTON, or NULL if BUTTON is not a wheel
   button.  */
static const struct remote_wheel *
remote_wheel_of (uint8_t button)
{
  for (int i = 0; i < countof (remote_wheels); i++)
    if (remote_wheels[i].button == button)
      return &remote_wheels[i];
  return NULL;
}

/* Clear the mark of a motion of each frame of DPYINFO.  The
   mouse_position_hook gives the position, so the motion is not new
   after it.  */
static void
remote_clear_mouse_moved (struct remote_display_info *dpyinfo)
{
  Lisp_Object tail, frame;
  FOR_EACH_FRAME (tail, frame)
    if (FRAME_REMOTE_P (XFRAME (frame))
	&& FRAME_DISPLAY_INFO (XFRAME (frame)) == dpyinfo)
      XFRAME (frame)->mouse_moved = false;
}

/* Set the frame, the position X, Y, and the time TIME of the event
   IE to F.  */
static void
remote_set_position (struct input_event *ie, struct frame *f, int32_t x,
		     int32_t y, uint32_t time)
{
  XSETFRAME (ie->frame_or_window, f);
  XSETINT (ie->x, x);
  XSETINT (ie->y, y);
  ie->timestamp = time;
  ie->arg = Qnil;
}

/* Return true if KEYSYM is a key with no character for Emacs: a
   function, cursor, keypad, or other special key, or a vendor key.  A
   modifier key is not such a key: it gives no event.  */
static bool
remote_function_keysym_p (uint32_t keysym)
{
  return (((keysym >= 0xfe00 && keysym <= 0xffff)
	   || (keysym & KEYSYM_VENDOR))
	  && !remote_modifier_keysym_p (keysym));
}

/* Return true if KEYSYM is a modifier key, or a lock key of an ISO
   keyboard group.  */
static bool
remote_modifier_keysym_p (uint32_t keysym)
{
  return ((keysym >= 0xffe1 && keysym <= 0xffee)
	  || (keysym >= 0xfe01 && keysym <= 0xfe0f));
}

/* The modifier of Emacs of each bit of the modifiers of a message.  */
static const struct
{
  uint8_t bit;
  int modifier;
} remote_modifiers[] =
  {
    { RD_SHIFT, shift_modifier },
    { RD_CONTROL, ctrl_modifier },
    { RD_META, meta_modifier },
    { RD_SUPER, super_modifier },
    { RD_HYPER, hyper_modifier },
    { RD_ALT, alt_modifier },
  };

/* Return the modifiers of Emacs for the bits MODIFIERS of a
   message.  */
static int
remote_emacs_modifiers (uint8_t modifiers)
{
  int emacs_modifiers = 0;
  for (int i = 0; i < countof (remote_modifiers); i++)
    if (modifiers & remote_modifiers[i].bit)
      emacs_modifiers |= remote_modifiers[i].modifier;
  return emacs_modifiers;
}

/* Return the kind of the event of the character C.  */
static enum event_kind
remote_character_kind (int c)
{
  return ASCII_CHAR_P (c) ? ASCII_KEYSTROKE_EVENT
	 : MULTIBYTE_CHAR_KEYSTROKE_EVENT;
}
