dnl The installed name of Emacs, and the directory of its DOC file.

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

dnl test/install/program-name.sh runs these macros in a small
dnl configure script, and checks them against the Makefile.

dnl emacs_PROGRAM_NAME
dnl Set emacs_program_name to the installed name of the program
dnl "emacs", after --program-prefix, --program-suffix and
dnl --program-transform-name.  AC_ARG_PROGRAM must come first.  It
dnl doubles each "\" and "$" of the transform for make, so undo that.
AC_DEFUN([emacs_PROGRAM_NAME],
[AC_REQUIRE([AC_ARG_PROGRAM])
emacs_transform=`printf '%s\n' "$program_transform_name" \
  | sed 's/\([[\\$]]\)\1/\1/g'`
emacs_program_name=`echo emacs | sed "$emacs_transform"`
dnl emacs_ETCDOCDIR puts the name in etcdocdir.  Make expands "$" in
dnl it, and the sed commands of the Makefile cannot have "&", ";", or
dnl "\" in it, so accept only a simple name.
case $emacs_program_name in
  emacs) ;;
  '' | *[[!A-Za-z0-9._+-]]*)
    AC_MSG_ERROR(["$emacs_program_name" is not a valid name of the program.
Use only letters, digits, ".", "_", "+", and "-" in the name that
--program-prefix, --program-suffix, and --program-transform-name give.]) ;;
esac
AC_SUBST([emacs_program_name])
])

dnl emacs_ETCDOCDIR
dnl Set etcdocdir, the directory of the DOC file.  Each configuration
dnl has its own DOC file.  A renamed program, for example
dnl "emacs-remote" from --program-suffix=-remote, can share its prefix
dnl with the "emacs" of another configuration.  An install deletes
dnl each DOC file in etcdir, so the DOC file of a renamed program goes
dnl in a directory of its own.
AC_DEFUN([emacs_ETCDOCDIR],
[AC_REQUIRE([emacs_PROGRAM_NAME])
etcdocdir='${datadir}/emacs/${version}/etc'
if test "$emacs_program_name" != emacs; then
  etcdocdir='${datadir}/emacs/${version}/etc-'"$emacs_program_name"
fi
])
