/* Popup menus of a remote display session.

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

/* The client shows a popup menu with its toolkit.  menu.c puts the
   menu in menu_items (keyboard.h).  This file walks menu_items into a
   list of entries, and sends one menu-item message for each entry, in
   order, and then one show-menu message (rdproto.h).  Then it waits
   for the menu-choice message of the client.  The wait lets process
   filters and timers run, as sit-for does.  A change of the client
   ends the wait as a dismissal.  Each exit from the wait sends a
   hide-menu message, so the client never keeps a menu that the
   session does not wait for.  A chosen item gives its selection, as
   in the other menu_show_hook functions.

   The entries of a submenu come between a submenu-start entry, with
   the label of the submenu, and a submenu-end entry.  The id of an
   item is its index in menu_items.  */

#include <config.h>

#include "lisp.h"
#include "coding.h"
#include "keyboard.h"
#include "menu.h"
#include "rdproto.h"
#include "remoteterm.h"
#include "remotemenu.h"

/* The time of one step of the wait for the answer of the client, in
   nanoseconds.  */
enum { REMOTE_MENU_WAIT_NSECS = 10 * 1000 * 1000 };

/* The entries of a menu, in order.  ITEMS has SIZE elements, and the
   first COUNT are in use.  */
struct remote_menu_entries
{
  struct rd_menu_item *items;
  ptrdiff_t count;
  ptrdiff_t size;
};

/* The menu that a session waits for.  It is on the stack of
   remote_menu_show, and the menu state of the session points to it
   while the client shows the menu.  */
struct remote_menu_wait
{
  /* The session, or NULL after remote_drop_menu.  */
  struct remote_display_info *dpyinfo;

  /* The entries that the session sent.  */
  struct remote_menu_entries entries;

  /* True if the wait ends.  */
  bool answered;

  /* True if the user selected the item ID.  */
  bool chosen;
  uint32_t id;
};

static void remote_show_menu (struct frame *, struct remote_menu_wait *,
			      int, int, int, Lisp_Object);
static void remote_wait_menu_answer (struct remote_menu_wait *);
static Lisp_Object remote_menu_answer_value (struct remote_menu_wait *,
					     int);
static void remote_end_menu (void *);
static void remote_hold_menu_items (void);
static void remote_answer_menu (struct remote_menu_wait *, bool, uint32_t);
static bool remote_enabled_item_p (struct remote_menu_entries *, uint32_t);
static Lisp_Object remote_menu_selection (uint32_t, int);
static void remote_send_menu_entries (struct remote_display_info *,
				      uint32_t, struct remote_menu_entries *);
static void remote_send_show_menu (struct frame *, uint32_t, int, int,
				   Lisp_Object);
static void remote_send_hide_menu (struct remote_display_info *,
				   uint32_t);
static void remote_menu_entries (int, struct remote_menu_entries *);
static bool remote_add_menu_pane (struct remote_menu_entries *, int,
				  bool);
static void remote_add_menu_item (struct remote_menu_entries *, int);
static bool remote_submenu_follows_p (int);
static uint8_t remote_menu_item_flags (int);
static void remote_add_menu_entry (struct remote_menu_entries *,
				   enum rd_menu_kind, uint8_t, uint32_t,
				   Lisp_Object, Lisp_Object);
static uint16_t remote_menu_text (Lisp_Object, uint8_t *, ptrdiff_t);
static ptrdiff_t remote_utf8_prefix (const unsigned char *, ptrdiff_t,
				     ptrdiff_t);

Lisp_Object
remote_menu_show (struct frame *f, int x, int y, int menuflags,
		  Lisp_Object title, const char **error_name)
{
  if (menu_items_used <= MENU_ITEMS_PANE_LENGTH)
    {
      *error_name = "Empty menu";
      return Qnil;
    }
  specpdl_ref count = SPECPDL_INDEX ();
  struct remote_menu_wait wait = { .dpyinfo = FRAME_DISPLAY_INFO (f) };
  record_unwind_protect_ptr (remote_end_menu, &wait);
  remote_hold_menu_items ();
  if (remote_frame_has_client (f))
    {
      remote_show_menu (f, &wait, x, y, menuflags, title);
      remote_wait_menu_answer (&wait);
    }
  return unbind_to (count, remote_menu_answer_value (&wait, menuflags));
}

void
remote_menu_choice_input (struct remote_display_info *dpyinfo,
			  const struct rd_message *message)
{
  struct remote_menu_wait *wait = dpyinfo->menu.wait;
  const struct rd_menu_choice *choice = &message->u.menu_choice;
  if (wait && choice->serial == dpyinfo->menu.serial
      && (!choice->chosen || remote_enabled_item_p (&wait->entries,
						    choice->id)))
    remote_answer_menu (wait, choice->chosen, choice->id);
}

void
remote_drop_menu (struct remote_display_info *dpyinfo)
{
  struct remote_menu_wait *wait = dpyinfo->menu.wait;
  if (wait)
    {
      remote_answer_menu (wait, false, 0);
      wait->dpyinfo = NULL;
      dpyinfo->menu.wait = NULL;
    }
}

/* Send the entries of menu_items and the show-menu message for the
   frame F to its client, at the pixel position X, Y of F, with the
   title TITLE.  Keep the entries in WAIT, and make WAIT the menu that
   the session waits for.  MENUFLAGS are as for menu_show_hook.  */
static void
remote_show_menu (struct frame *f, struct remote_menu_wait *wait, int x,
		  int y, int menuflags, Lisp_Object title)
{
  struct remote_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  uint32_t serial = ++dpyinfo->menu.serial;
  remote_menu_entries (menuflags, &wait->entries);
  remote_send_menu_entries (dpyinfo, serial, &wait->entries);
  remote_send_show_menu (f, serial, x, y, title);
  remote_flush (dpyinfo);
  dpyinfo->menu.wait = wait;
}

/* Wait until the client answers the menu of WAIT, or the session drops
   it.  Let process filters and timers run while the session waits.  */
static void
remote_wait_menu_answer (struct remote_menu_wait *wait)
{
  while (!wait->answered)
    {
      wait_reading_process_output (0, REMOTE_MENU_WAIT_NSECS, 0, false,
				   Qnil, NULL, 0);
      maybe_quit ();
    }
}

/* Return the value of the menu of WAIT for menu_show_hook.  This is
   the selection of the chosen item.  After a dismissal, it is nil if
   MENUFLAGS has MENU_FOR_CLICK, and otherwise signal quit, as
   haikumenu.c does.  */
static Lisp_Object
remote_menu_answer_value (struct remote_menu_wait *wait, int menuflags)
{
  if (wait->chosen)
    return remote_menu_selection (wait->id, menuflags);
  if (!(menuflags & MENU_FOR_CLICK))
    quit ();
  return Qnil;
}

/* The unwind function of remote_menu_show for the menu WAIT (a struct
   remote_menu_wait).  If the client shows the menu, tell it to close
   the menu.  */
static void
remote_end_menu (void *data)
{
  struct remote_menu_wait *wait = data;
  struct remote_display_info *dpyinfo = wait->dpyinfo;
  if (dpyinfo && dpyinfo->menu.wait == wait)
    {
      dpyinfo->menu.wait = NULL;
      remote_send_hide_menu (dpyinfo, dpyinfo->menu.serial);
      remote_flush (dpyinfo);
    }
  xfree (wait->entries.items);
}

/* Keep menu_items in use until the unwind.  x_popup_menu_1 stops its
   use before it calls menu_show_hook, but the selection needs it after
   the wait.  A menu from a timer or a process filter during the wait
   then signals an error in init_menu_items, and does not replace
   menu_items.  */
static void
remote_hold_menu_items (void)
{
  record_unwind_protect_void (unuse_menu_items);
  menu_items_inuse = true;
}

/* End the wait of WAIT.  CHOSEN is true if the user selected the item
   ID.  */
static void
remote_answer_menu (struct remote_menu_wait *wait, bool chosen, uint32_t id)
{
  wait->answered = true;
  wait->chosen = chosen;
  wait->id = id;
}

/* Return true if ID is the id of an enabled item of ENTRIES.  */
static bool
remote_enabled_item_p (struct remote_menu_entries *entries, uint32_t id)
{
  for (ptrdiff_t i = 0; i < entries->count; i++)
    {
      struct rd_menu_item *item = &entries->items[i];
      if (item->kind == RD_MENU_KIND_ITEM && item->id == id)
	return (item->flags & RD_MENU_FLAG_ENABLED) != 0;
    }
  return false;
}

/* Return the selection of the item at the index ID of menu_items.  In
   a keymap menu (MENUFLAGS has MENU_KEYMAPS), this is the list of the
   keys from the menu to the item.  Otherwise, it is the value of the
   item.  This is the walk of haikumenu.c.  */
static Lisp_Object
remote_menu_selection (uint32_t id, int menuflags)
{
  USE_SAFE_ALLOCA;
  Lisp_Object *subprefix_stack;
  SAFE_NALLOCA (subprefix_stack, 1, menu_items_used);
  Lisp_Object prefix = Qnil, entry = Qnil;
  int submenu_depth = 0;
  int i = 0;
  while (i < menu_items_used && i != id)
    {
      Lisp_Object slot = AREF (menu_items, i);
      if (NILP (slot))
	{
	  subprefix_stack[submenu_depth++] = prefix;
	  prefix = entry;
	  i++;
	}
      else if (EQ (slot, Qlambda))
	{
	  prefix = subprefix_stack[--submenu_depth];
	  i++;
	}
      else if (EQ (slot, Qt))
	{
	  prefix = AREF (menu_items, i + MENU_ITEMS_PANE_PREFIX);
	  i += MENU_ITEMS_PANE_LENGTH;
	}
      else if (EQ (slot, Qquote))
	i++;
      else
	{
	  entry = AREF (menu_items, i + MENU_ITEMS_ITEM_VALUE);
	  i += MENU_ITEMS_ITEM_LENGTH;
	}
    }
  entry = AREF (menu_items, i + MENU_ITEMS_ITEM_VALUE);
  if (menuflags & MENU_KEYMAPS)
    {
      entry = list1 (entry);
      if (!NILP (prefix))
	entry = Fcons (prefix, entry);
      for (int j = submenu_depth - 1; j >= 0; j--)
	if (!NILP (subprefix_stack[j]))
	  entry = Fcons (subprefix_stack[j], entry);
    }
  SAFE_FREE ();
  return entry;
}

/* Send a menu-item message with SERIAL for each of ENTRIES to the
   client of DPYINFO.  */
static void
remote_send_menu_entries (struct remote_display_info *dpyinfo,
			  uint32_t serial, struct remote_menu_entries *entries)
{
  for (ptrdiff_t i = 0; i < entries->count; i++)
    {
      struct rd_message message =
	{
	  .type = RD_MENU_ITEM,
	  .u.menu_item = entries->items[i],
	};
      message.u.menu_item.serial = serial;
      remote_send_message (dpyinfo, &message);
    }
}

/* Send the show-menu message SERIAL for the frame F, at the pixel
   position X, Y of F, with the title TITLE.  */
static void
remote_send_show_menu (struct frame *f, uint32_t serial, int x, int y,
		       Lisp_Object title)
{
  struct rd_message message =
    {
      .type = RD_SHOW_MENU,
      .u.show_menu = { .frame = FRAME_NATIVE_WINDOW (f), .serial = serial,
		       .x = x, .y = y },
    };
  message.u.show_menu.ntitle
    = remote_menu_text (title, message.u.show_menu.title, RD_MAX_MENU_TEXT);
  remote_send_message (FRAME_DISPLAY_INFO (f), &message);
}

/* Send the hide-menu message SERIAL to the client of DPYINFO.  */
static void
remote_send_hide_menu (struct remote_display_info *dpyinfo, uint32_t serial)
{
  struct rd_message message =
    {
      .type = RD_HIDE_MENU,
      .u.hide_menu = { .serial = serial },
    };
  remote_send_message (dpyinfo, &message);
}

/* Add the entries of menu_items to ENTRIES, in order.  MENUFLAGS are
   as for menu_show_hook.  A submenu of menu_items gives its start
   entry, its entries, and its end entry.  In a menu that is a list of
   panes and not a keymap, each pane with a name is a submenu, if the
   menu has more than one pane.  */
static void
remote_menu_entries (int menuflags, struct remote_menu_entries *entries)
{
  bool pane_submenus = !(menuflags & MENU_KEYMAPS) && menu_items_n_panes > 1;
  bool pane_open = false;
  int i = 0;
  while (i < menu_items_used)
    {
      Lisp_Object slot = AREF (menu_items, i);
      if (NILP (slot))
	i++;
      else if (EQ (slot, Qlambda))
	{
	  remote_add_menu_entry (entries, RD_MENU_KIND_SUBMENU_END, 0, 0,
				 Qnil, Qnil);
	  i++;
	}
      else if (EQ (slot, Qquote))
	i++;
      else if (EQ (slot, Qt))
	{
	  if (pane_submenus)
	    pane_open = remote_add_menu_pane (entries, i, pane_open);
	  i += MENU_ITEMS_PANE_LENGTH;
	}
      else
	{
	  remote_add_menu_item (entries, i);
	  i += MENU_ITEMS_ITEM_LENGTH;
	}
    }
  if (pane_open)
    remote_add_menu_entry (entries, RD_MENU_KIND_SUBMENU_END, 0, 0,
			   Qnil, Qnil);
}

/* Add the entries of the pane at the index I of menu_items to ENTRIES.
   PANE_OPEN is true if the pane before it is an open submenu.  End
   that submenu, and start a submenu if the pane has a name.  Return
   true if the pane is an open submenu.  */
static bool
remote_add_menu_pane (struct remote_menu_entries *entries, int i,
		      bool pane_open)
{
  Lisp_Object name = AREF (menu_items, i + MENU_ITEMS_PANE_NAME);
  if (pane_open)
    remote_add_menu_entry (entries, RD_MENU_KIND_SUBMENU_END, 0, 0,
			   Qnil, Qnil);
  if (!STRINGP (name) || SCHARS (name) == 0)
    return false;
  remote_add_menu_entry (entries, RD_MENU_KIND_SUBMENU_START,
			 RD_MENU_FLAG_ENABLED, 0, name, Qnil);
  return true;
}

/* Add the entry of the item at the index I of menu_items to ENTRIES.
   An item that a submenu follows is the start of the submenu.  */
static void
remote_add_menu_item (struct remote_menu_entries *entries, int i)
{
  Lisp_Object name = AREF (menu_items, i + MENU_ITEMS_ITEM_NAME);
  if (remote_submenu_follows_p (i))
    remote_add_menu_entry (entries, RD_MENU_KIND_SUBMENU_START,
			   RD_MENU_FLAG_ENABLED, 0, name, Qnil);
  else if (STRINGP (name) && menu_separator_name_p (SSDATA (name)))
    remote_add_menu_entry (entries, RD_MENU_KIND_SEPARATOR, 0, 0,
			   Qnil, Qnil);
  else
    remote_add_menu_entry (entries, RD_MENU_KIND_ITEM,
			   remote_menu_item_flags (i), i, name,
			   AREF (menu_items, i + MENU_ITEMS_ITEM_EQUIV_KEY));
}

/* Return true if a submenu follows the item at the index I of
   menu_items.  */
static bool
remote_submenu_follows_p (int i)
{
  int next = i + MENU_ITEMS_ITEM_LENGTH;
  return next < menu_items_used && NILP (AREF (menu_items, next));
}

/* Return the flags (enum rd_menu_flag) of the item at the index I of
   menu_items.  An item is enabled only if it has a definition.  */
static uint8_t
remote_menu_item_flags (int i)
{
  Lisp_Object type = AREF (menu_items, i + MENU_ITEMS_ITEM_TYPE);
  bool enabled = (!NILP (AREF (menu_items, i + MENU_ITEMS_ITEM_ENABLE))
		  && !NILP (AREF (menu_items, i + MENU_ITEMS_ITEM_DEFINITION)));
  bool selected = !NILP (AREF (menu_items, i + MENU_ITEMS_ITEM_SELECTED));
  return ((enabled ? RD_MENU_FLAG_ENABLED : 0)
	  | (EQ (type, QCtoggle) ? RD_MENU_FLAG_TOGGLE : 0)
	  | (EQ (type, QCradio) ? RD_MENU_FLAG_RADIO : 0)
	  | (selected ? RD_MENU_FLAG_SELECTED : 0));
}

/* Add an entry of KIND with FLAGS, ID, the text LABEL, and the key
   text KEY to ENTRIES.  LABEL and KEY are strings, or nil for no
   text.  */
static void
remote_add_menu_entry (struct remote_menu_entries *entries,
		       enum rd_menu_kind kind, uint8_t flags, uint32_t id,
		       Lisp_Object label, Lisp_Object key)
{
  if (entries->count == entries->size)
    entries->items = xpalloc (entries->items, &entries->size, 1, -1,
			      sizeof *entries->items);
  struct rd_menu_item *item = &entries->items[entries->count++];
  item->serial = 0;
  item->kind = kind;
  item->flags = flags;
  item->id = id;
  item->nlabel = remote_menu_text (label, item->label, RD_MAX_MENU_TEXT);
  item->nkey = remote_menu_text (key, item->key, RD_MAX_MENU_KEY);
}

/* Copy the UTF-8 bytes of TEXT to BYTES, and return their number.
   Copy at most LIMIT bytes: cut a longer text at the start of a
   character.  A TEXT that is not a string gives no bytes.  */
static uint16_t
remote_menu_text (Lisp_Object text, uint8_t *bytes, ptrdiff_t limit)
{
  if (!STRINGP (text))
    return 0;
  if (STRING_MULTIBYTE (text))
    text = ENCODE_UTF_8 (text);
  ptrdiff_t size = remote_utf8_prefix (SDATA (text), SBYTES (text), limit);
  memcpy (bytes, SDATA (text), size);
  return size;
}

/* Return the number of bytes of the longest start of the SIZE bytes
   of UTF-8 at BYTES that has at most LIMIT bytes and ends at the start
   of a character.  */
static ptrdiff_t
remote_utf8_prefix (const unsigned char *bytes, ptrdiff_t size,
		    ptrdiff_t limit)
{
  if (size <= limit)
    return size;
  ptrdiff_t end = limit;
  while (end > 0 && (bytes[end] & 0xc0) == 0x80)
    end--;
  return end;
}
