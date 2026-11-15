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

     (hello :version 1)

   The names in Lisp come from the table of formats of rdproto.h: an
   underscore becomes a hyphen, and a field name gets a colon.  So a
   new message type needs no change in this file.  */

#include <config.h>

#include <stddef.h>

#include "lisp.h"
#include "rdproto.h"

/* The Lisp form of each message type, indexed by enum rd_type: its
   symbol, and then the keywords of its fields.  */
static Lisp_Object lisp_formats[RD_TYPE_LIMIT];

static struct rd_message message_from_lisp (Lisp_Object);
static Lisp_Object message_to_lisp (const struct rd_message *);
static enum rd_type type_of_symbol (Lisp_Object);
static void array_from_lisp (const struct rd_field *, Lisp_Object,
			     struct rd_message *);
static Lisp_Object array_to_lisp (const struct rd_field *,
				  const struct rd_message *);
static void value_from_lisp (const struct rd_field *, int, Lisp_Object,
			     struct rd_message *);
static Lisp_Object value_to_lisp (const struct rd_field *, int,
				  const struct rd_message *);
static ptrdiff_t byte_index (Lisp_Object, Lisp_Object);
static Lisp_Object lisp_format (const struct rd_format *);
static Lisp_Object lisp_name (const char *, const char *);

DEFUN ("remote-display--encode", Fremote_display__encode,
       Sremote_display__encode, 1, 1, 0,
       doc: /* Return the bytes of MESSAGE as a unibyte string.
MESSAGE is a list such as (hello :version 1).  The variable
`remote-display--message-formats' gives the fields of each type.  */)
  (Lisp_Object message)
{
  struct rd_message m = message_from_lisp (message);
  Lisp_Object bytes = make_uninit_string (rd_encoded_size (&m));
  rd_encode (&m, SDATA (bytes));
  return bytes;
}

DEFUN ("remote-display--decode", Fremote_display__decode,
       Sremote_display__decode, 1, 2, 0,
       doc: /* Decode the first message in the unibyte string BYTES.
The message starts at the index START of BYTES, by default 0.  Return
(MESSAGE . USED), where USED is the number of bytes of the message.
Return nil if BYTES holds only the start of a message.  Signal
`remote-display-protocol-error' if BYTES is not valid.  The error data
is a symbol for the cause.  For `unknown-type', a second element gives
the number of bytes of the frame, so a receiver can skip it.  */)
  (Lisp_Object bytes, Lisp_Object start)
{
  CHECK_STRING (bytes);
  if (STRING_MULTIBYTE (bytes))
    error ("BYTES must be a unibyte string");
  ptrdiff_t first = NILP (start) ? 0 : byte_index (bytes, start);
  struct rd_message m;
  size_t used;
  enum rd_status status = rd_decode (SDATA (bytes) + first,
				     SBYTES (bytes) - first, &m, &used);

  /* This switch has no default case, so the compiler warns if a status
     has no case.  */
  switch (status)
    {
    case RD_OK:
      return Fcons (message_to_lisp (&m), make_fixnum (used));
    case RD_INCOMPLETE:
      return Qnil;
    case RD_UNKNOWN_TYPE:
      xsignal2 (Qremote_display_protocol_error, Qunknown_type,
		make_fixnum (used));
    case RD_BAD_LENGTH:
      xsignal1 (Qremote_display_protocol_error, Qbad_length);
    case RD_FRAME_TOO_LONG:
      xsignal1 (Qremote_display_protocol_error, Qframe_too_long);
    }

  /* The bytes come from the network, so a status with no case must not
     stop Emacs.  */
  xsignal1 (Qremote_display_protocol_error, make_fixnum (status));
}

/* Return the integer START, which must be an index of the unibyte
   string BYTES or its length.  */
static ptrdiff_t
byte_index (Lisp_Object bytes, Lisp_Object start)
{
  CHECK_FIXNUM (start);
  if (! (0 <= XFIXNUM (start) && XFIXNUM (start) <= SBYTES (bytes)))
    args_out_of_range (bytes, start);
  return XFIXNUM (start);
}

/* Convert the Lisp form of a message to the C form.  */
static struct rd_message
message_from_lisp (Lisp_Object message)
{
  enum rd_type type = type_of_symbol (CAR (message));
  if (type == RD_TYPE_NONE)
    xsignal1 (Qremote_display_protocol_error, message);

  Lisp_Object values = CDR (message);
  const struct rd_format *format = &rd_formats[type];
  Lisp_Object keywords = XCDR (lisp_formats[type]);
  struct rd_message m;
  m.type = type;
  for (int i = 0; i < format->nfields; i++, keywords = XCDR (keywords))
    {
      const struct rd_field *f = &format->fields[i];
      Lisp_Object value = plist_get (values, XCAR (keywords));
      if (f->max_count)
	array_from_lisp (f, value, &m);
      else
	value_from_lisp (f, 0, value, &m);
    }
  return m;
}

/* Convert the C form of a message to the Lisp form.  */
static Lisp_Object
message_to_lisp (const struct rd_message *m)
{
  const struct rd_format *format = &rd_formats[m->type];
  Lisp_Object keywords = XCDR (lisp_formats[m->type]);
  Lisp_Object message = list1 (XCAR (lisp_formats[m->type]));
  Lisp_Object tail = message;
  for (int i = 0; i < format->nfields; i++, keywords = XCDR (keywords))
    {
      const struct rd_field *f = &format->fields[i];
      Lisp_Object value = (f->max_count ? array_to_lisp (f, m)
			   : value_to_lisp (f, 0, m));
      Lisp_Object property = list2 (XCAR (keywords), value);
      XSETCDR (tail, property);
      tail = XCDR (property);
    }
  return message;
}

/* Return the message type with the symbol SYMBOL, or RD_TYPE_NONE if
   SYMBOL is not a message type.  */
static enum rd_type
type_of_symbol (Lisp_Object symbol)
{
  for (int type = RD_TYPE_NONE + 1; type < RD_TYPE_LIMIT; type++)
    if (EQ (symbol, XCAR (lisp_formats[type])))
      return type;
  return RD_TYPE_NONE;
}

/* Set the array F of M from VECTOR.  Signal an error if VECTOR has more
   elements than F can hold.  */
static void
array_from_lisp (const struct rd_field *f, Lisp_Object vector,
		 struct rd_message *m)
{
  CHECK_VECTOR (vector);
  ptrdiff_t n = ASIZE (vector);
  if (n > f->max_count)
    args_out_of_range (make_fixnum (n), make_fixnum (f->max_count));
  rd_set_array_count (m, f, n);
  for (ptrdiff_t i = 0; i < n; i++)
    value_from_lisp (f, i, AREF (vector, i), m);
}

/* Return the elements of the array F of M as a vector.  */
static Lisp_Object
array_to_lisp (const struct rd_field *f, const struct rd_message *m)
{
  ptrdiff_t n = rd_array_count (m, f);
  Lisp_Object vector = make_nil_vector (n);
  for (ptrdiff_t i = 0; i < n; i++)
    ASET (vector, i, value_to_lisp (f, i, m));
  return vector;
}

/* Set the element INDEX of F in M to the integer VALUE.  Signal an
   error if VALUE is not an integer in the limits of F.  */
static void
value_from_lisp (const struct rd_field *f, int index, Lisp_Object value,
		 struct rd_message *m)
{
  rd_set_field_value (m, f, index,
		      check_integer_range (value, f->value->min,
					   f->value->max));
}

/* Return the element INDEX of F in M.  */
static Lisp_Object
value_to_lisp (const struct rd_field *f, int index,
	       const struct rd_message *m)
{
  return make_int (rd_field_value (m, f, index));
}

/* Return the Lisp form of FORMAT: the symbol, then the keywords.  */
static Lisp_Object
lisp_format (const struct rd_format *format)
{
  Lisp_Object keywords = Qnil;
  for (int i = format->nfields - 1; i >= 0; i--)
    keywords = Fcons (lisp_name (":", format->fields[i].name), keywords);
  return Fcons (lisp_name ("", format->name), keywords);
}

/* Return the symbol PREFIX + C_NAME, with a hyphen for each
   underscore.  */
static Lisp_Object
lisp_name (const char *prefix, const char *c_name)
{
  Lisp_Object name = concat2 (build_string (prefix), build_string (c_name));
  for (ptrdiff_t i = 0; i < SBYTES (name); i++)
    if (SREF (name, i) == '_')
      SSET (name, i, '-');
  return Fintern (name, Qnil);
}

void
syms_of_remotefns (void)
{
  DEFSYM (Qremote_display, "remote-display");

  DEFSYM (Qremote_display_protocol_error, "remote-display-protocol-error");
  DEFSYM (Qunknown_type, "unknown-type");
  DEFSYM (Qbad_length, "bad-length");
  DEFSYM (Qframe_too_long, "frame-too-long");
  define_error (Qremote_display_protocol_error,
		"Remote display protocol error", Qerror);

  DEFVAR_LISP ("remote-display--max-glyphs", Vremote_display__max_glyphs,
	       doc: /* The maximum number of glyphs in a `draw-glyphs' message.  */);
  Vremote_display__max_glyphs = make_fixnum (RD_MAX_GLYPHS);
  make_symbol_constant (intern_c_string ("remote-display--max-glyphs"));

  DEFVAR_LISP ("remote-display--max-fringe-rows",
	       Vremote_display__max_fringe_rows,
	       doc: /* The maximum number of rows in a `draw-fringe' message.  */);
  Vremote_display__max_fringe_rows = make_fixnum (RD_MAX_FRINGE_ROWS);
  make_symbol_constant (intern_c_string ("remote-display--max-fringe-rows"));

  DEFVAR_LISP ("remote-display--hash-size", Vremote_display__hash_size,
	       doc: /* The number of bytes of the hash of a font file.  */);
  Vremote_display__hash_size = make_fixnum (RD_HASH_SIZE);
  make_symbol_constant (intern_c_string ("remote-display--hash-size"));

  DEFVAR_LISP ("remote-display--max-font-chunk",
	       Vremote_display__max_font_chunk,
	       doc: /* The maximum number of bytes in a `font-data' message.  */);
  Vremote_display__max_font_chunk = make_fixnum (RD_MAX_FONT_CHUNK);
  make_symbol_constant (intern_c_string ("remote-display--max-font-chunk"));

  DEFVAR_LISP ("remote-display--modifier-bits",
	       Vremote_display__modifier_bits,
	       doc: /* The bits of the modifiers field of an input message.
Each element is (MODIFIER . BIT).  */);
  Lisp_Object bits = Qnil;
#define MODIFIER_ENTRY(ENUM, NAME, BIT)					\
  bits = Fcons (Fcons (intern_c_string (#NAME), make_fixnum (BIT)), bits);
  RD_MODIFIERS (MODIFIER_ENTRY)
#undef MODIFIER_ENTRY
  Vremote_display__modifier_bits = Fnreverse (bits);
  make_symbol_constant (intern_c_string ("remote-display--modifier-bits"));

  /* The list for Lisp is a copy, so a change of it does not change
     the conversion of messages.  */
  Lisp_Object formats = Qnil;
  lisp_formats[RD_TYPE_NONE] = Qnil;
  staticpro (&lisp_formats[RD_TYPE_NONE]);
  for (int type = RD_TYPE_LIMIT - 1; type > RD_TYPE_NONE; type--)
    {
      lisp_formats[type] = lisp_format (&rd_formats[type]);
      staticpro (&lisp_formats[type]);
      formats = Fcons (Fcopy_sequence (lisp_formats[type]), formats);
    }
  DEFVAR_LISP ("remote-display--message-formats",
	       Vremote_display__message_formats,
	       doc: /* The message types of the protocol, in the order of their numbers.
Each element is (TYPE KEYWORD...): the symbol of the message type, and
the keywords of its fields.  */);
  Vremote_display__message_formats = formats;
  make_symbol_constant (intern_c_string ("remote-display--message-formats"));

  defsubr (&Sremote_display__encode);
  defsubr (&Sremote_display__decode);

  Fprovide (Qremote_display, Qnil);
}
