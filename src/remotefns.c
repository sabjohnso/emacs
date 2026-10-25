/* Lisp interface to the remote display protocol.

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

/* In Lisp, a message is a list: the message type, then a property
   list of its fields.  For example:

     (hello :version 1)  */

#include <config.h>

#include "lisp.h"
#include "rdproto.h"

static struct rd_message message_from_lisp (Lisp_Object);
static Lisp_Object message_to_lisp (const struct rd_message *);
static uint16_t uint16_field (Lisp_Object, Lisp_Object);
static Lisp_Object status_symbol (enum rd_status);

DEFUN ("remote-display--encode", Fremote_display__encode,
       Sremote_display__encode, 1, 1, 0,
       doc: /* Return the bytes of MESSAGE as a unibyte string.
MESSAGE is a list such as (hello :version 1).  */)
  (Lisp_Object message)
{
  struct rd_message m = message_from_lisp (message);
  Lisp_Object bytes = make_uninit_string (rd_encoded_size (&m));
  rd_encode (&m, SDATA (bytes));
  return bytes;
}

DEFUN ("remote-display--decode", Fremote_display__decode,
       Sremote_display__decode, 1, 1, 0,
       doc: /* Decode the first message in the unibyte string BYTES.
Return (MESSAGE . USED), where USED is the number of bytes of the
message.  Return nil if BYTES holds only the start of a message.
Signal `remote-display-protocol-error' if BYTES is not valid.  The
error data is a symbol for the cause.  */)
  (Lisp_Object bytes)
{
  CHECK_STRING (bytes);
  if (STRING_MULTIBYTE (bytes))
    error ("BYTES must be a unibyte string");
  struct rd_message m;
  size_t used;
  enum rd_status status = rd_decode (SDATA (bytes), SBYTES (bytes),
				     &m, &used);
  switch (status)
    {
    case RD_OK:
      return Fcons (message_to_lisp (&m), make_fixnum (used));
    case RD_INCOMPLETE:
      return Qnil;
    default:
      xsignal1 (Qremote_display_protocol_error, status_symbol (status));
    }
}

/* Convert the Lisp form of a message to the C form.  */
static struct rd_message
message_from_lisp (Lisp_Object message)
{
  Lisp_Object type = CAR (message);
  Lisp_Object fields = CDR (message);
  struct rd_message m;
  if (EQ (type, Qremote_display_hello))
    {
      m.type = RD_HELLO;
      m.u.hello.version = uint16_field (fields, QCversion);
      return m;
    }
  xsignal1 (Qremote_display_protocol_error, message);
}

/* Convert the C form of a message to the Lisp form.  */
static Lisp_Object
message_to_lisp (const struct rd_message *m)
{
  switch (m->type)
    {
    case RD_HELLO:
      return list3 (Qremote_display_hello, QCversion,
		    make_fixnum (m->u.hello.version));
    }
  emacs_abort ();
}

/* Return the value of property NAME in FIELDS.  Signal an error if
   it is not a 16-bit unsigned integer.  */
static uint16_t
uint16_field (Lisp_Object fields, Lisp_Object name)
{
  Lisp_Object value = plist_get (fields, name);
  CHECK_FIXNUM (value);
  if (! (0 <= XFIXNUM (value) && XFIXNUM (value) <= UINT16_MAX))
    args_out_of_range (value, make_fixnum (UINT16_MAX));
  return XFIXNUM (value);
}

/* Return the symbol for an error STATUS of rd_decode.  */
static Lisp_Object
status_symbol (enum rd_status status)
{
  switch (status)
    {
    case RD_UNKNOWN_TYPE:
      return Qunknown_type;
    case RD_BAD_LENGTH:
      return Qbad_length;
    case RD_FRAME_TOO_LONG:
      return Qframe_too_long;
    default:
      emacs_abort ();
    }
}

void
syms_of_remotefns (void)
{
  DEFSYM (Qremote_display, "remote-display");
  DEFSYM (Qremote_display_hello, "hello");
  DEFSYM (QCversion, ":version");

  DEFSYM (Qremote_display_protocol_error, "remote-display-protocol-error");
  DEFSYM (Qunknown_type, "unknown-type");
  DEFSYM (Qbad_length, "bad-length");
  DEFSYM (Qframe_too_long, "frame-too-long");
  define_error (Qremote_display_protocol_error,
		"Remote display protocol error", Qerror);

  defsubr (&Sremote_display__encode);
  defsubr (&Sremote_display__decode);

  Fprovide (Qremote_display, Qnil);
}
