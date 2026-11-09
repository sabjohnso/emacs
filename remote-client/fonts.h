/* The font sources of the remote display client.

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

#ifndef REMOTE_CLIENT_FONTS_H
#define REMOTE_CLIENT_FONTS_H

#include "render.h"

/* Return a new font source that gives the face of the fontconfig
   match of "monospace" for each font.  A glyph code is an index in the
   font of the server, so this source draws the correct glyphs only if
   the match is the font of the server.  If fontconfig has no match,
   FIND of the source returns NULL.  */
extern struct rc_font_source rc_fallback_font_source_new (void);

#endif /* REMOTE_CLIENT_FONTS_H */
