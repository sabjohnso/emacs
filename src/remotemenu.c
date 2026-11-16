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
   filters and timers run, as sit-for does.

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

static void remote_send_menu_entries (struct remote_display_info *,
				      uint32_t, int);
static void remote_send_show_menu (struct frame *, uint32_t, int, int,
				   Lisp_Object);
static void remote_wait_menu_answer (struct remote_display_info *);
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
  struct remote_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  uint32_t serial = ++dpyinfo->menu.serial;
  remote_send_menu_entries (dpyinfo, serial, menuflags);
  remote_send_show_menu (f, serial, x, y, title);
  remote_flush (dpyinfo);
  remote_wait_menu_answer (dpyinfo);
  return Qnil;
}

void
remote_menu_choice_input (struct remote_display_info *dpyinfo,
			  const struct rd_message *message)
{
  dpyinfo->menu.answered = true;
}

/* Send a menu-item message with SERIAL for each entry of menu_items
   to the client of DPYINFO.  MENUFLAGS are as for menu_show_hook.  */
static void
remote_send_menu_entries (struct remote_display_info *dpyinfo,
			  uint32_t serial, int menuflags)
{
  struct remote_menu_entries entries = { NULL, 0, 0 };
  remote_menu_entries (menuflags, &entries);
  for (ptrdiff_t i = 0; i < entries.count; i++)
    {
      struct rd_message message =
	{
	  .type = RD_MENU_ITEM,
	  .u.menu_item = entries.items[i],
	};
      message.u.menu_item.serial = serial;
      remote_send_message (dpyinfo, &message);
    }
  xfree (entries.items);
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

/* Wait until the client of DPYINFO answers the menu.  Let process
   filters and timers run while the session waits.  */
static void
remote_wait_menu_answer (struct remote_display_info *dpyinfo)
{
  dpyinfo->menu.answered = false;
  while (!dpyinfo->menu.answered)
    {
      wait_reading_process_output (0, REMOTE_MENU_WAIT_NSECS, 0, false,
				   Qnil, NULL, 0);
      maybe_quit ();
    }
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
