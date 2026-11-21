dnl The client program of the remote display, emacs-remote-client.

dnl Copyright (C) 2026 Free Software Foundation, Inc.
dnl This file is part of GNU Emacs.

dnl GNU Emacs is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation, either version 3 of the License, or
dnl (at your option) any later version.

dnl GNU Emacs is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.

dnl You should have received a copy of the GNU General Public License
dnl along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.

dnl test/install/client-option.sh runs this macro in a small configure
dnl script.

dnl emacs_REMOTE_CLIENT
dnl Decide if "make" makes the client program in remote-client, and set
dnl HAVE_REMOTE_CLIENT to "yes" or "no".  The program uses RC_BASE
dnl (cairo, cairo-ft, fontconfig, freetype2, and glib-2.0), RC_TLS
dnl (gnutls), and RC_GTK (gtk+-3.0).  Only its tests use RC_X11 (x11
dnl and xtst).  The option --with-remote-client is "ifavailable" by
dnl default: the build makes the client if it finds the libraries, and
dnl continues without the client if not.  With --with-remote-client,
dnl configure stops if a library is missing.
dnl
dnl HAVE_REMOTE_DISPLAY must be set first.  A build with the remote
dnl display needs the client, because the relay on the host of the
dnl session is the same program.  Thus such a build stops if a library
dnl is missing, and does not accept --without-remote-client.
AC_DEFUN([emacs_REMOTE_CLIENT],
[AC_ARG_WITH([remote-client],
  [AS_HELP_STRING([--without-remote-client],
    [do not make the client program of the remote display
     (emacs-remote-client); by default, make it if its libraries
     are present])],
  [], [with_remote_client=ifavailable])
case $with_remote_client in
  yes | no | ifavailable) ;;
  *) AC_MSG_ERROR(["$with_remote_client" is not a valid value of --with-remote-client.
Use yes, no, or ifavailable.]) ;;
esac
HAVE_REMOTE_CLIENT=no
if test "$with_remote_client" = no; then
  if test "$HAVE_REMOTE_DISPLAY" = yes; then
    AC_MSG_ERROR([--with-remote-display needs the client program
emacs-remote-client, because its relay runs on the host of the
session.  Do not use --without-remote-client with it.])
  fi
else
  EMACS_CHECK_MODULES([RC_BASE],
    [cairo cairo-ft fontconfig freetype2 glib-2.0])
  EMACS_CHECK_MODULES([RC_TLS], [gnutls])
  EMACS_CHECK_MODULES([RC_GTK], [gtk+-3.0])
  EMACS_CHECK_MODULES([RC_X11], [x11 xtst])
  if test "$HAVE_RC_BASE$HAVE_RC_TLS$HAVE_RC_GTK" = yesyesyes; then
    HAVE_REMOTE_CLIENT=yes
  elif test "$HAVE_REMOTE_DISPLAY" = yes \
       || test "$with_remote_client" = yes; then
    AC_MSG_ERROR([The client program of the remote display needs the
pkg-config packages cairo, cairo-ft, fontconfig, freetype2, glib-2.0,
gnutls, and gtk+-3.0.  Install their development files, or configure
without --with-remote-display and --with-remote-client.])
  else
    AC_MSG_NOTICE([the client program of the remote display is not made,
because pkg-config does not find all of cairo, cairo-ft, fontconfig,
freetype2, glib-2.0, gnutls, and gtk+-3.0])
  fi
fi
AC_SUBST([HAVE_REMOTE_CLIENT])
])
