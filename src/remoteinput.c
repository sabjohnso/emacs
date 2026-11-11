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
   one.  */

#include <config.h>

#include "lisp.h"
#include "character.h"
#include "frame.h"
#include "keyboard.h"
#include "termhooks.h"
#include "remoteinput.h"

/* Keysyms with this bit are vendor keys.  */
enum { KEYSYM_VENDOR = 1 << 28 };

static void remote_key_event (struct remote_display_info *,
			      const struct rd_key *);
static bool remote_function_keysym_p (uint32_t);
static bool remote_modifier_keysym_p (uint32_t);
static int remote_emacs_modifiers (uint8_t);
static enum event_kind remote_character_kind (int);

void
remote_handle_input (struct remote_display_info *dpyinfo,
		     const struct rd_message *message)
{
  switch (message->type)
    {
    case RD_KEY:
      remote_key_event (dpyinfo, &message->u.key);
      break;
    default:
      break;
    }
}

/* Store the key event of KEY for its frame of DPYINFO.  */
static void
remote_key_event (struct remote_display_info *dpyinfo,
		  const struct rd_key *key)
{
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
