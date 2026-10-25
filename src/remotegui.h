/* Window-system types of the remote display backend.

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

/* The generic display code uses these Xlib names.  The remote
   backend has no Xlib, so this file defines them.  */

#ifndef EMACS_REMOTEGUI_H
#define EMACS_REMOTEGUI_H

#include <stdint.h>

typedef struct _XCharStruct
{
  int rbearing;
  int lbearing;
  int width;
  int ascent;
  int descent;
} XCharStruct;

/* A mouse cursor.  The generic code uses NULL for no cursor.  */
typedef struct remote_cursor *Emacs_Cursor;

/* A window is the frame ID that the client knows.  */
typedef int Window;

/* The remote backend has no display object.  */
typedef struct remote_display Display;

#define WINDOW_HANDLE_UINTPTR(h) ((uintptr_t) (h))

/* The remote backend has no X resources.  */
typedef void *XrmDatabase;

typedef struct
{
  int x, y;
  unsigned width, height;
} XRectangle;

/* frame.c uses these.  */
#define ForgetGravity		0
#define NorthWestGravity	1
#define NorthGravity		2
#define NorthEastGravity	3
#define WestGravity		4
#define CenterGravity		5
#define EastGravity		6
#define SouthWestGravity	7
#define SouthGravity		8
#define SouthEastGravity	9
#define StaticGravity		10

#define NoValue		0x0000
#define XValue  	0x0001
#define YValue		0x0002
#define WidthValue  	0x0004
#define HeightValue  	0x0008
#define AllValues 	0x000F
#define XNegative 	0x0010
#define YNegative 	0x0020

#define USPosition	(1L << 0)
#define USSize		(1L << 1)
#define PPosition	(1L << 2)
#define PSize		(1L << 3)
#define PMinSize	(1L << 4)
#define PMaxSize	(1L << 5)
#define PResizeInc	(1L << 6)
#define PAspect		(1L << 7)
#define PBaseSize	(1L << 8)
#define PWinGravity	(1L << 9)

#define NativeRectangle XRectangle

#define CONVERT_TO_EMACS_RECT(xr, nr)		\
  ((xr).x     = (nr).x,				\
   (xr).y     = (nr).y,				\
   (xr).width = (nr).width,			\
   (xr).height = (nr).height)

#define CONVERT_FROM_EMACS_RECT(xr, nr)		\
  ((nr).x      = (xr).x,			\
   (nr).y      = (xr).y,			\
   (nr).width  = (xr).width,			\
   (nr).height = (xr).height)

#define STORE_NATIVE_RECT(nr, px, py, pwidth, pheight)	\
  ((nr).x      = (px),					\
   (nr).y      = (py),					\
   (nr).width  = (pwidth),				\
   (nr).height = (pheight))

#endif /* EMACS_REMOTEGUI_H */
