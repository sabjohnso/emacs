/* Message codec of the remote display protocol.

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

/* This codec does not use Lisp objects, so the server (Emacs) and
   the client can both use it.

   A message on the wire is a frame:

     length  4 bytes, big-endian: the number of bytes that follow
     type    1 byte: an enum rd_type value
     body    LENGTH - 1 bytes: the fields of the message type

   Each field is big-endian, and a signed field is two's complement.
   An array is its number of elements, 2 bytes, and then its elements.
   Positions and sizes of pixels have 32 bits, so the size of a frame
   does not limit them.  A color is 32 bits 0xRRGGBB.  The sender sets
   the high 8 bits to 0, and a receiver ignores them.  LENGTH is at
   most RD_MAX_LENGTH.

   A later version of the protocol can append fields to a message
   type.  A decoder reads the fields that it knows, and ignores the
   bytes that follow them.  A later version can also append message
   types.  A decoder reports RD_UNKNOWN_TYPE for such a frame, with its
   size, so the receiver can skip it.

   Each message type is one row of RD_MESSAGES and one list of fields.
   The enum rd_type value, the C struct, the wire layout, and the Lisp
   form all come from that one place.  */

#ifndef EMACS_RDPROTO_H
#define EMACS_RDPROTO_H

#include <stddef.h>
#include <stdint.h>

enum { RD_MAX_LENGTH = 1 << 24 };

/* The maximum number of glyphs in one draw-glyphs message.  The
   server sends a longer run of glyphs in more than one message.  */
enum { RD_MAX_GLYPHS = 512 };

/* The maximum number of rows of pixels in one draw-fringe message.
   The height of a fringe bitmap of Emacs has 8 bits.  */
enum { RD_MAX_FRINGE_ROWS = 255 };

/* The number of bytes of a SHA-256 hash, which identifies a font
   file.  */
enum { RD_HASH_SIZE = 32 };

/* The maximum number of bytes of a font file in one font-data
   message.  Each message struct has room for the largest array, so a
   larger chunk makes each struct larger.  */
enum { RD_MAX_FONT_CHUNK = 8192 };

/* The maximum number of bytes of a clipboard text in one
   clipboard-data message, and the maximum size of a clipboard text.
   The chunk has the size of a font chunk, so no message struct gets
   larger.  */
enum { RD_MAX_CLIPBOARD_CHUNK = RD_MAX_FONT_CHUNK };
enum { RD_MAX_CLIPBOARD_SIZE = 16 * 1024 * 1024 };

/* The kinds of cursor.  A client ignores a kind that it does not
   know.  */
enum rd_cursor_kind
  {
    RD_CURSOR_BOX = 0,
    RD_CURSOR_HOLLOW_BOX = 1,
    RD_CURSOR_BAR = 2,
    RD_CURSOR_HBAR = 3,
  };

/* The message types.  A row is MESSAGE (ENUM, NAME, FIELDS): the name
   of the enum rd_type value is RD_ENUM, and the struct is struct
   rd_NAME.  FIELDS is the list of its fields.  The type byte of a row
   is its position, starting at 1, so a new type goes at the end of its
   block.  The rows come in blocks, one block for each feature, in a
   fixed order.  A feature adds its rows only to its own block.  Each
   block after the base has its own section below, with its field lists
   and its limits.  */
#define RD_MESSAGES(MESSAGE)						\
  RD_BASE_MESSAGES (MESSAGE)						\
  RD_IMAGE_MESSAGES (MESSAGE)						\
  RD_CHILD_FRAME_MESSAGES (MESSAGE)					\
  RD_MENU_MESSAGES (MESSAGE)						\
  RD_SCALE_MESSAGES (MESSAGE)						\
  RD_SESSION_MESSAGES (MESSAGE)

/* The messages of the base protocol.  */
#define RD_BASE_MESSAGES(MESSAGE)					\
  MESSAGE (HELLO, hello, RD_HELLO_FIELDS)				\
  MESSAGE (DRAW_GLYPHS, draw_glyphs, RD_DRAW_GLYPHS_FIELDS)		\
  MESSAGE (CLEAR_AREA, clear_area, RD_CLEAR_AREA_FIELDS)		\
  MESSAGE (COPY_AREA, copy_area, RD_COPY_AREA_FIELDS)			\
  MESSAGE (DRAW_CURSOR, draw_cursor, RD_DRAW_CURSOR_FIELDS)		\
  MESSAGE (DRAW_FRINGE, draw_fringe, RD_DRAW_FRINGE_FIELDS)		\
  MESSAGE (DEFINE_FONT, define_font, RD_DEFINE_FONT_FIELDS)		\
  MESSAGE (DEFINE_FRAME, define_frame, RD_DEFINE_FRAME_FIELDS)		\
  MESSAGE (DELETE_FRAME, delete_frame, RD_DELETE_FRAME_FIELDS)		\
  MESSAGE (KEY, key, RD_KEY_FIELDS)					\
  MESSAGE (BUTTON, button, RD_BUTTON_FIELDS)				\
  MESSAGE (MOTION, motion, RD_MOTION_FIELDS)				\
  MESSAGE (FOCUS, focus, RD_FOCUS_FIELDS)				\
  MESSAGE (RESIZE, resize, RD_RESIZE_FIELDS)				\
  MESSAGE (PING, ping, RD_PING_FIELDS)					\
  MESSAGE (PONG, pong, RD_PONG_FIELDS)					\
  MESSAGE (CLOSE_FRAME, close_frame, RD_CLOSE_FRAME_FIELDS)		\
  MESSAGE (WANT_FONT, want_font, RD_WANT_FONT_FIELDS)			\
  MESSAGE (FONT_DATA, font_data, RD_FONT_DATA_FIELDS)			\
  MESSAGE (CLIPBOARD_DATA, clipboard_data, RD_CLIPBOARD_DATA_FIELDS)

/* Each list gives the fields of a message in the order of the wire.
   FIELD (M, TYPE, NAME) is a member NAME of the C type TYPE_t.
   ARRAY (M, TYPE, NAME, COUNT, MAX) is the member COUNT, a uint16_t
   with the number of elements, and the array NAME of MAX elements.
   TYPE is one of uint8, uint16, uint32, and int32.  M is the name
   of the message, for the macro that reads the list.  */

/* The first message of each side of a connection.  */
#define RD_HELLO_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint16, version)

/* Draw glyphs of the font FONT in COLOR.  X and Y are the pixel
   position of the start of the baseline.  Each glyph code is a glyph index of
   the font, not a character.  Each glyph after the first starts at the
   advance of the glyph before it, rounded to a pixel, as in
   ftcrfont_draw.  A client changes only the pixels inside
   the rectangle at CLIP_X, CLIP_Y with the size CLIP_WIDTH,
   CLIP_HEIGHT, because a glyph can extend outside a line that is
   visible in part, or outside a text area.  A client draws only
   the glyphs, and not a background, because the glyphs of a composition
   overlap.  The server sends a clear-area message first.  */
#define RD_DRAW_GLYPHS_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, int32, clip_x)						\
  FIELD (M, int32, clip_y)						\
  FIELD (M, uint32, clip_width)						\
  FIELD (M, uint32, clip_height)					\
  FIELD (M, uint32, color)						\
  FIELD (M, uint32, font)						\
  ARRAY (M, uint32, glyphs, nglyphs, RD_MAX_GLYPHS)

/* Fill a rectangle with COLOR.  X and Y are the pixel position of the
   top left corner.  */
#define RD_CLEAR_AREA_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint32, width)						\
  FIELD (M, uint32, height)						\
  FIELD (M, uint32, color)

/* Copy the rectangle at X, Y with the size WIDTH, HEIGHT to the
   position TO_X, TO_Y of the same frame.  The rectangles can
   overlap.  */
#define RD_COPY_AREA_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint32, width)						\
  FIELD (M, uint32, height)						\
  FIELD (M, int32, to_x)						\
  FIELD (M, int32, to_y)

/* Draw a cursor of the rd_cursor_kind KIND in COLOR in the rectangle
   at X, Y with the size WIDTH, HEIGHT.  The server erases a cursor: it clears
   the area under the cursor, and draws its glyphs again.  */
#define RD_DRAW_CURSOR_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint32, width)						\
  FIELD (M, uint32, height)						\
  FIELD (M, uint8, kind)						\
  FIELD (M, uint32, color)

/* Draw a fringe bitmap at X, Y.  The message holds the rows of the
   bitmap, so a client needs no table of bitmaps when it connects
   again.  In each element of ROWS, the bits from 0 to WIDTH - 1 are
   the pixels, and bit 0 is the rightmost pixel, so WIDTH has 16 bits
   and not the 32 bits of the other sizes.  A bit that is 1 has the
   color FOREGROUND, and a bit that is 0 has the color BACKGROUND.  If
   OVERLAY is not zero, draw only the bits that are 1, on the pixels
   that are there.  If CURSOR is not zero,
   the bitmap is the cursor.  */
#define RD_DRAW_FRINGE_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint16, width)						\
  FIELD (M, uint8, overlay)						\
  FIELD (M, uint8, cursor)						\
  FIELD (M, uint32, foreground)						\
  FIELD (M, uint32, background)						\
  ARRAY (M, uint16, rows, nrows, RD_MAX_FRINGE_ROWS)

/* Give the font ID FONT to the face INDEX of the font
   file whose SHA-256 hash is HASH, at the size of SIZE pixels.  A
   session sends this before the first draw-glyphs message with FONT
   after each attach of a client, so a client that connects again
   needs no state from an earlier connection.  An ID keeps its font
   for the life of the server.  A hash with fewer than RD_HASH_SIZE
   bytes means that the server could not read the file.  The file has
   FILE_SIZE bytes, so a client can hash only its files of that size.  */
#define RD_DEFINE_FONT_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, font)						\
  FIELD (M, uint32, index)						\
  FIELD (M, uint32, size)						\
  ARRAY (M, uint8, hash, nhash, RD_HASH_SIZE)				\
  FIELD (M, uint32, file_size)

/* The frame FRAME exists, and has the size WIDTH, HEIGHT in pixels.
   A session sends this before the first draw of a frame after each
   attach of a client, and again before the next draw after each change
   of the size.  */
#define RD_DEFINE_FRAME_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, uint32, width)						\
  FIELD (M, uint32, height)

/* The frame FRAME does not exist now.  A session sends this only for
   a frame that it defined after the last attach of the client.  */
#define RD_DELETE_FRAME_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)

/* The messages from a client to the server follow.  Each has the
   frame of the window where the event happened.  TIME is the time of
   the client in milliseconds, which can wrap.  MODIFIERS has the bits
   of enum rd_modifier.  */

/* The user pressed the key KEYSYM, an X keysym.  CHARACTER is the
   Unicode character of the key, or 0 if it has none.  */
#define RD_KEY_FIELDS(FIELD, ARRAY, M)					\
  FIELD (M, uint32, frame)						\
  FIELD (M, uint32, time)						\
  FIELD (M, uint32, keysym)						\
  FIELD (M, uint32, character)						\
  FIELD (M, uint8, modifiers)

/* The user pressed (PRESS is 1) or released (PRESS is 0) the mouse
   button BUTTON at X, Y.  Button 1 is the left button.  The buttons of
   enum rd_wheel_button are the wheel, as in X.  */
#define RD_BUTTON_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, uint32, time)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint8, button)						\
  FIELD (M, uint8, modifiers)						\
  FIELD (M, uint8, press)

/* The mouse moved to X, Y.  */
#define RD_MOTION_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, uint32, time)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)

/* The window of FRAME got (IN is 1) or lost (IN is 0) the focus.  */
#define RD_FOCUS_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, uint8, in)

/* The window of FRAME has the size WIDTH, HEIGHT in pixels now.  */
#define RD_RESIZE_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, uint32, width)						\
  FIELD (M, uint32, height)

/* The messages that keep a connection alive follow.  A client sends
   a ping when the connection is quiet, and the server answers with a
   pong that has the same SERIAL.  Thus a client knows that the
   connection is lost when no bytes come for some time.  */

/* A client asks for a pong with SERIAL.  */
#define RD_PING_FIELDS(FIELD, ARRAY, M)					\
  FIELD (M, uint32, serial)

/* The server answers the ping with SERIAL.  */
#define RD_PONG_FIELDS(FIELD, ARRAY, M)					\
  FIELD (M, uint32, serial)

/* A client message: the user asked to close the window of FRAME, for
   example with the close button of the window manager.  The client
   keeps the window.  The server decides whether to delete FRAME, as
   Emacs decides for the WM_DELETE_WINDOW message of X.  */
#define RD_CLOSE_FRAME_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)

/* The messages that send a font file follow.  A client that does not
   have the file of a define-font message asks for it with its hash.
   The server sends the file in font-data messages, in order, from the
   offset 0.  */

/* A client message: send the font file with the hash HASH.  */
#define RD_WANT_FONT_FIELDS(FIELD, ARRAY, M)				\
  ARRAY (M, uint8, hash, nhash, RD_HASH_SIZE)

/* The bytes BYTES of the font file with the hash HASH, from OFFSET.
   The file has TOTAL bytes.  */
#define RD_FONT_DATA_FIELDS(FIELD, ARRAY, M)				\
  ARRAY (M, uint8, hash, nhash, RD_HASH_SIZE)				\
  FIELD (M, uint32, offset)						\
  FIELD (M, uint32, total)						\
  ARRAY (M, uint8, bytes, nbytes, RD_MAX_FONT_CHUNK)

/* The message of the clipboard, which both sides send.  When the
   clipboard of one side changes, that side sends the UTF-8 text in
   clipboard-data messages, in order, from the offset 0.  BYTES has the
   bytes from OFFSET of the text SERIAL, which has TOTAL bytes.  A
   message with a new SERIAL starts a new text, and a receiver drops a
   text that is not complete.  rdchunk.h splits and joins the
   messages.  */
#define RD_CLIPBOARD_DATA_FIELDS(FIELD, ARRAY, M)			\
  FIELD (M, uint32, serial)						\
  FIELD (M, uint32, offset)						\
  FIELD (M, uint32, total)						\
  ARRAY (M, uint8, bytes, nbytes, RD_MAX_CLIPBOARD_CHUNK)

/* Images (PLAN.org feature-images).  */

/* The messages of images.  The server decodes each image, and sends
   its pixels as a PNG file in image-data messages.  The client keeps
   the pixels of each image ID, and paints them for a draw-image
   message.  An ID is unique for the life of the server process.  */
#define RD_IMAGE_MESSAGES(MESSAGE)					\
  MESSAGE (DRAW_IMAGE, draw_image, RD_DRAW_IMAGE_FIELDS)		\
  MESSAGE (IMAGE_DATA, image_data, RD_IMAGE_DATA_FIELDS)		\
  MESSAGE (FORGET_IMAGE, forget_image, RD_FORGET_IMAGE_FIELDS)

/* The maximum number of bytes of a PNG file in one image-data
   message, and the maximum size of a PNG file.  The chunk has the
   size of a font chunk, so no message struct gets larger.  */
enum { RD_MAX_IMAGE_CHUNK = RD_MAX_FONT_CHUNK };
enum { RD_MAX_IMAGE_SIZE = 64 * 1024 * 1024 };

/* The maximum number of pixels on each side of an image.  A client
   does not decode a PNG file with more.  */
enum { RD_MAX_IMAGE_SIDE = 8192 };

/* Paint the pixels of IMAGE with its top-left pixel at X, Y of FRAME,
   over the pixels that are there.  A client changes only the pixels
   inside the rectangle at CLIP_X, CLIP_Y with the size CLIP_WIDTH,
   CLIP_HEIGHT.  The server sends a clear-area message first, so a
   transparent pixel shows the background.  An image that the client
   does not have changes no pixel.  */
#define RD_DRAW_IMAGE_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, uint32, image)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, int32, clip_x)						\
  FIELD (M, int32, clip_y)						\
  FIELD (M, uint32, clip_width)						\
  FIELD (M, uint32, clip_height)

/* The bytes BYTES from OFFSET of the PNG file with the pixels of
   IMAGE.  The file has TOTAL bytes, at most RD_MAX_IMAGE_SIZE.  The
   server sends the messages of one image in order, from the offset 0.
   rdchunk.h splits and joins the messages.  An ID keeps its pixels
   until a forget-image message or the end of the connection.  */
#define RD_IMAGE_DATA_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, image)						\
  FIELD (M, uint32, offset)						\
  FIELD (M, uint32, total)						\
  ARRAY (M, uint8, bytes, nbytes, RD_MAX_IMAGE_CHUNK)

/* The server does not use IMAGE again.  The client frees its pixels,
   and ignores an ID that it does not have.  The server never uses an
   ID again, so this message cannot remove a newer image.  */
#define RD_FORGET_IMAGE_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, image)

/* Tooltips and child frames (PLAN.org feature-child-frames).  */

/* The messages of child frames.  */
#define RD_CHILD_FRAME_MESSAGES(MESSAGE)				\
  MESSAGE (PLACE_FRAME, place_frame, RD_PLACE_FRAME_FIELDS)

/* The place of the frame FRAME, which the server sends.  PARENT is
   the frame that contains FRAME, or 0 if FRAME has a window of its
   own.  X and Y are the position of the top-left corner of FRAME in
   the surface of PARENT, from the origin of PARENT.  A client ignores
   X and Y when PARENT is 0.  Z is the stack value among the frames
   with the same parent: a frame with a larger Z is above.  The server
   does not send equal Z values for two such frames.  If a client gets
   equal values, the frame with the larger number is above.  VISIBLE
   is 0 if the frame is hidden.  A frame with no place-frame message
   is a visible frame with a window of its own.  The server sends
   place-frame for a new frame before its first define-frame.  A
   client does not show a frame whose parent it does not know, or
   whose parent or other ancestor is hidden.  */
#define RD_PLACE_FRAME_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, uint32, parent)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  FIELD (M, uint32, z)							\
  FIELD (M, uint8, visible)

/* Menus drawn by Emacs (PLAN.org feature-menus).  */

/* The messages of popup menus.  The server sends the entries of a
   menu in menu-item messages, and then a show-menu message.  The
   client answers with one menu-choice message, unless a hide-menu
   message comes first.  */
#define RD_MENU_MESSAGES(MESSAGE)					\
  MESSAGE (MENU_ITEM, menu_item, RD_MENU_ITEM_FIELDS)			\
  MESSAGE (SHOW_MENU, show_menu, RD_SHOW_MENU_FIELDS)			\
  MESSAGE (HIDE_MENU, hide_menu, RD_HIDE_MENU_FIELDS)			\
  MESSAGE (MENU_CHOICE, menu_choice, RD_MENU_CHOICE_FIELDS)

/* The maximum number of bytes of the UTF-8 text of a label or a
   title.  The server cuts a longer text at the start of a
   character.  */
enum { RD_MAX_MENU_TEXT = 1024 };

/* The maximum number of bytes of the UTF-8 text of a key
   equivalent.  */
enum { RD_MAX_MENU_KEY = 128 };

/* The kinds of a menu entry.  The submenu-start and submenu-end
   entries enclose the entries of a submenu.  */
enum rd_menu_kind
  {
    RD_MENU_KIND_ITEM = 0,
    RD_MENU_KIND_SEPARATOR = 1,
    RD_MENU_KIND_SUBMENU_START = 2,
    RD_MENU_KIND_SUBMENU_END = 3,
  };

/* The bits of the FLAGS field of a menu entry.  */
enum rd_menu_flag
  {
    RD_MENU_FLAG_ENABLED = 1,
    RD_MENU_FLAG_TOGGLE = 2,
    RD_MENU_FLAG_RADIO = 4,
    RD_MENU_FLAG_SELECTED = 8,
  };

/* One entry of the menu SERIAL, in order.  KIND is an enum
   rd_menu_kind value, and FLAGS has the bits of enum rd_menu_flag.
   ID identifies the item in the menu, and is 0 for a kind that is not
   an item.  LABEL is the UTF-8 text of the entry, and KEY is the
   UTF-8 text of its key equivalent.  An entry with a new SERIAL makes
   the client drop the entries of an older menu that it did not
   show.  */
#define RD_MENU_ITEM_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, serial)						\
  FIELD (M, uint8, kind)						\
  FIELD (M, uint8, flags)						\
  FIELD (M, uint32, id)							\
  ARRAY (M, uint8, label, nlabel, RD_MAX_MENU_TEXT)			\
  ARRAY (M, uint8, key, nkey, RD_MAX_MENU_KEY)

/* Show the menu SERIAL with the UTF-8 title TITLE at the pixel
   position X, Y of the window of FRAME.  TITLE is empty when the menu
   has no title.  A client that has no window for FRAME, or whose
   entries of SERIAL do not nest correctly, answers at once with a
   menu-choice message with CHOSEN 0.  */
#define RD_SHOW_MENU_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, frame)						\
  FIELD (M, uint32, serial)						\
  FIELD (M, int32, x)							\
  FIELD (M, int32, y)							\
  ARRAY (M, uint8, title, ntitle, RD_MAX_MENU_TEXT)

/* Close the menu SERIAL if it is open, and drop its entries.  The
   client then sends no choice for SERIAL.  The client ignores this
   message for a menu that is not open.  */
#define RD_HIDE_MENU_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, serial)

/* A client message: the answer to the show-menu message SERIAL.
   CHOSEN is 1 if the user selected the item ID, and 0 if the menu
   closed with no selection.  The server ignores ID when CHOSEN is 0,
   and ignores a choice for a menu that it does not wait for.  */
#define RD_MENU_CHOICE_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint32, serial)						\
  FIELD (M, uint8, chosen)						\
  FIELD (M, uint32, id)

/* HiDPI scale.  */

/* The messages of the scale.  A client shows the frames on a desktop
   that has a scale: each logical pixel of the desktop has SCALE times
   SCALE device pixels.  The positions and the sizes of the protocol
   are device pixels, so Emacs draws text of the correct physical
   size, and the client paints each pixel of a frame on one device
   pixel.  */
#define RD_SCALE_MESSAGES(MESSAGE)					\
  MESSAGE (SCALE, scale, RD_SCALE_FIELDS)

/* The largest scale that a server uses.  */
enum { RD_MAX_SCALE = 8 };

/* A client message: the scale of the client is SCALE.  A client sends
   this message on each connection, before the input of the user, and
   again after each change of its scale.  The server ignores a SCALE
   of 0 or more than RD_MAX_SCALE.  */
#define RD_SCALE_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint8, scale)

/* The messages of the end of a session.  When a session ends, the
   server sends this message, and then closes the connection.  Thus the
   client can tell the end of the session from the loss of the
   connection.  The relay of remote-client also sends it when the
   session does not exist.  */
#define RD_SESSION_MESSAGES(MESSAGE)					\
  MESSAGE (END_SESSION, end_session, RD_END_SESSION_FIELDS)

/* The reasons of an end-session message.  A client takes a reason
   that it does not know as RD_END_NO_SESSION.  */
enum rd_end_reason
  {
    /* The session ended on purpose.  */
    RD_END_CLOSED = 0,
    /* The session does not exist, for example because Emacs stopped.  */
    RD_END_NO_SESSION = 1,
  };

/* A server message: the session ended for the reason REASON, an enum
   rd_end_reason value.  The client closes its windows, and does not
   connect again.  The connection closes after this message.  */
#define RD_END_SESSION_FIELDS(FIELD, ARRAY, M)				\
  FIELD (M, uint8, reason)

/* The chunked message types (rdchunk.h).  A row is CHUNKED (ENUM,
   NAME, SERIAL): the type RD_ENUM, and the member NAME of the union of
   struct rd_message.  SERIAL is the field that identifies the text.
   The other fields of the type are offset and total, and the array
   bytes with its count nbytes.  The type also has a row in its block
   of RD_MESSAGES.  A new chunked type adds one row here.  rdchunk.c
   uses one chunk size, so each chunked type has chunks of
   RD_MAX_CLIPBOARD_CHUNK bytes.  */
#define RD_CHUNKED_MESSAGES(CHUNKED)					\
  CHUNKED (CLIPBOARD_DATA, clipboard_data, serial)			\
  CHUNKED (IMAGE_DATA, image_data, image)

/* The buttons of a button message that are the mouse wheel.  */
enum rd_wheel_button
  {
    RD_BUTTON_WHEEL_UP = 4,
    RD_BUTTON_WHEEL_DOWN = 5,
    RD_BUTTON_WHEEL_LEFT = 6,
    RD_BUTTON_WHEEL_RIGHT = 7,
  };

/* The modifiers of an input message.  A row is MODIFIER (ENUM, NAME,
   BIT): the bit RD_ENUM of the MODIFIERS field, and the NAME of the
   modifier in Lisp.  The bits are part of the wire format.  */
#define RD_MODIFIERS(MODIFIER)						\
  MODIFIER (SHIFT, shift, 1)						\
  MODIFIER (CONTROL, control, 2)					\
  MODIFIER (META, meta, 4)						\
  MODIFIER (SUPER, super, 8)						\
  MODIFIER (HYPER, hyper, 16)						\
  MODIFIER (ALT, alt, 32)

#define RD_MODIFIER_ENUMERATOR(ENUM, NAME, BIT) RD_##ENUM = BIT,
enum rd_modifier
  {
    RD_MODIFIERS (RD_MODIFIER_ENUMERATOR)
  };
#undef RD_MODIFIER_ENUMERATOR

/* The value 0 is not a message type.  */
#define RD_TYPE_ENUMERATOR(ENUM, NAME, FIELDS) RD_##ENUM,
enum rd_type
  {
    RD_TYPE_NONE,
    RD_MESSAGES (RD_TYPE_ENUMERATOR)
    /* One more than the last message type.  */
    RD_TYPE_LIMIT
  };
#undef RD_TYPE_ENUMERATOR

#define RD_MEMBER(M, TYPE, NAME) TYPE##_t NAME;
#define RD_ARRAY_MEMBER(M, TYPE, NAME, COUNT, MAX)	\
  uint16_t COUNT;					\
  TYPE##_t NAME[MAX];
#define RD_STRUCT(ENUM, NAME, FIELDS)				\
  struct rd_##NAME { FIELDS (RD_MEMBER, RD_ARRAY_MEMBER, NAME) };
RD_MESSAGES (RD_STRUCT)
#undef RD_STRUCT
#undef RD_ARRAY_MEMBER
#undef RD_MEMBER

struct rd_message
{
  enum rd_type type;
  union
  {
#define RD_UNION_MEMBER(ENUM, NAME, FIELDS) struct rd_##NAME NAME;
    RD_MESSAGES (RD_UNION_MEMBER)
#undef RD_UNION_MEMBER
  } u;
};

/* The form of a value of a field.  SIZE is its size in a struct and on
   the wire.  MIN and MAX are its limits.  */
struct rd_value_type
{
  size_t size;
  int64_t min;
  int64_t max;
  int64_t (*load) (const void *place);
  void (*store) (void *place, int64_t value);
};

/* One field of a message type.  NAME is the name of the member.
   OFFSET is its position in struct rd_message.  An array has MAX_COUNT
   above 0, and COUNT_OFFSET is the position of its number of
   elements.  */
struct rd_field
{
  const char *name;
  const struct rd_value_type *value;
  size_t offset;
  size_t count_offset;
  int max_count;
};

/* One message type: the name of its struct, and its NFIELDS fields in
   the order of the wire.  */
struct rd_format
{
  const char *name;
  const struct rd_field *fields;
  int nfields;
};

/* The message types, indexed by enum rd_type from RD_HELLO.  */
extern const struct rd_format rd_formats[RD_TYPE_LIMIT];

/* Return the number of elements of the array F of MESSAGE, and set
   that number to COUNT, which must be at most the MAX_COUNT of F.  */
extern int rd_array_count (const struct rd_message *message,
			   const struct rd_field *f);
extern void rd_set_array_count (struct rd_message *message,
				const struct rd_field *f, int count);

/* Return the element INDEX of the field F of MESSAGE, and set it to
   VALUE, which must be in the limits of F.  INDEX is 0 for a field
   that is not an array.  */
extern int64_t rd_field_value (const struct rd_message *message,
			       const struct rd_field *f, int index);
extern void rd_set_field_value (struct rd_message *message,
				const struct rd_field *f, int index,
				int64_t value);

enum rd_status
  {
    /* BUF holds a complete message.  */
    RD_OK,
    /* BUF holds only the start of a message.  Read more bytes.  */
    RD_INCOMPLETE,
    /* The type byte is not an enum rd_type value.  The frame can be
       from a later version of the protocol.  */
    RD_UNKNOWN_TYPE,
    /* The length is too small for the type, or a count in the
       message is too large.  */
    RD_BAD_LENGTH,
    /* The length is more than RD_MAX_LENGTH.  */
    RD_FRAME_TOO_LONG,
  };

/* Return the number of bytes that rd_encode writes for MESSAGE.  */
extern size_t rd_encoded_size (const struct rd_message *message);

/* Write MESSAGE to BUF, which has rd_encoded_size (MESSAGE) bytes.
   The values of MESSAGE must be in range, for example NGLYPHS at
   most RD_MAX_GLYPHS.  */
extern void rd_encode (const struct rd_message *message,
		       unsigned char *buf);

/* Return the end-session message with REASON, an enum rd_end_reason
   value.  Each sender of the end of a session makes it here.  */
extern struct rd_message rd_end_session_message (uint8_t reason);

/* Decode the first message in the LEN bytes at BUF.  If the result
   is RD_OK, set *MESSAGE to the message and *USED to the number of
   bytes that it occupies.  If the result is RD_UNKNOWN_TYPE, set *USED
   to the size of the frame, so a receiver can skip it.  That size can
   be more than LEN, and the receiver must then wait for the other
   bytes.  For any other result, *MESSAGE and *USED are not specified.
   Report an error as soon as the length and the type byte are present,
   before the rest of the frame.  */
extern enum rd_status rd_decode (const unsigned char *buf, size_t len,
				 struct rd_message *message, size_t *used);

#endif /* EMACS_RDPROTO_H */
