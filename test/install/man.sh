#!/bin/bash
# Check the install of the man page of the remote display client.

# Copyright (C) 2026 Free Software Foundation, Inc.

# This file is part of GNU Emacs.

# GNU Emacs is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# GNU Emacs is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.

# Usage: man.sh BUILDDIR
#
# BUILDDIR is a build that "make" made.  Each check installs in a new
# temporary DESTDIR, and takes seconds.  The facts:
#
# GZIP_PROG is the gzip program, or empty.  With configure
# --without-compress-install, its value in the Makefile starts with "#",
# so make takes it as a comment, and the variable is empty.
#
# - compress: for each GZIP_PROG, the install of the client gives one
#   page with the text of the source, also when an old page is there.
#   The page is compressed only if GZIP_PROG is the program.  The
#   uninstall removes the page.  A build with no client skips this.
# - install-man: for each GZIP_PROG, the top-level install-man installs
#   the page of Emacs, and no page of the client, for the name "emacs"
#   and for a name with a suffix.  Thus a build without the client has
#   no page of it.
# - uninstall: for each GZIP_PROG, the top-level uninstall of a build
#   without the client removes the page of Emacs, and keeps the page of
#   the client that another build installed.

main () {
  if [ $# != 1 ]; then
    echo "Usage: man.sh BUILDDIR" >&2
    exit 2
  fi
  builddir=$(cd "$1" && pwd) || exit 1
  work=$(mktemp -d) || exit 1
  trap 'rm -rf "$work"' EXIT
  trap 'exit 1' HUP INT TERM
  man1dir=$(variable man1dir) || exit 1
  [ -n "$man1dir" ] || fail "cannot read man1dir"
  page=$(variable abs_srcdir)/doc/man/emacs-remote-client.1
  [ -f "$page" ] || fail "no file $page"
  have_client=$(variable HAVE_REMOTE_CLIENT)
  gzip=$(command -v gzip) || fail "no gzip"
  check_all "$gzip" .1.gz .1
  check_all '' .1 .1.gz
  [ "$have_client" = yes ] \
    || echo "SKIP man.sh: the build has no client, so no compress check"
  echo "man.sh: all checks pass"
}

# Check each fact with the GZIP_PROG $1.  It gives pages with the suffix
# $2, and no pages with the suffix $3.
check_all () {
  check_install_man "$1" "$2" 's,x,x,' emacs
  check_install_man "$1" "$2" 's&$$&-suffix&' emacs-suffix
  check_uninstall "$1" "$2" "$3"
  [ "$have_client" != yes ] || check_compress "$1" "$2"
}

# Print the value of the variable $1 in the top Makefile of the build.
# The value is the last line, because make can first print the output
# of config.status.
variable () {
  printf 'print-variable:\n\t@echo $(%s)\n' "$1" \
    | make -s --no-print-directory -C "$builddir" -f Makefile -f - \
	   print-variable | tail -n 1
}

fail () {
  echo "FAIL man.sh: $*" >&2
  exit 1
}

# Print the pages of the client in the DESTDIR $1, sorted.
client_pages () {
  (cd "$1$man1dir" 2> /dev/null && find . -name '*remote-client*' | sort)
}

# Check install-man with the GZIP_PROG $1, which gives pages with the
# suffix $2, and with the program transform $3, which gives the name $4
# to Emacs.
check_install_man () {
  local destdir=$work/install-man
  rm -rf "$destdir"
  make -C "$builddir" install-man DESTDIR="$destdir" GZIP_PROG="$1" \
       TRANSFORM="$3" > "$work/log" 2>&1 \
    || { cat "$work/log" >&2; fail "install-man: make failed for $4"; }
  [ -f "$destdir$man1dir/$4$2" ] || fail "install-man: no page $4$2"
  [ -z "$(client_pages "$destdir")" ] \
    || fail "install-man: pages of the client for $4:" \
	    "$(client_pages "$destdir")"
}

# Check the uninstall of a build without the client with the GZIP_PROG
# $1.  It removes the page of Emacs with the suffix $2, and not the one
# with the suffix $3, and keeps the pages of the client.  ELN_DESTDIR
# is in DESTDIR, because it is not there on each system.
check_uninstall () {
  local destdir=$work/uninstall
  rm -rf "$destdir"
  mkdir -p "$destdir$man1dir" || exit 1
  : > "$destdir$man1dir/emacs.1"
  : > "$destdir$man1dir/emacs.1.gz"
  : > "$destdir$man1dir/emacs-remote-client.1"
  : > "$destdir$man1dir/emacs-remote-client.1.gz"
  make -C "$builddir" uninstall DESTDIR="$destdir" TRANSFORM='s,x,x,' \
       HAVE_REMOTE_CLIENT=no GZIP_PROG="$1" ELN_DESTDIR="$destdir/eln/" \
       > "$work/log" 2>&1 \
    || { cat "$work/log" >&2; fail "uninstall: make failed"; }
  [ ! -e "$destdir$man1dir/emacs$2" ] || fail "uninstall: emacs$2 stays"
  [ -e "$destdir$man1dir/emacs$3" ] || fail "uninstall: emacs$3 is gone"
  [ "$(client_pages "$destdir" | tr '\n' ' ')" \
      = "./emacs-remote-client.1 ./emacs-remote-client.1.gz " ] \
    || fail "uninstall: the pages of the client that stay:" \
	    "$(client_pages "$destdir")"
}

# Check the install and the uninstall of the client with the GZIP_PROG
# $1.  The page must have the suffix $2.
check_compress () {
  local destdir=$work/compress
  rm -rf "$destdir"
  mkdir -p "$destdir$man1dir" || exit 1
  echo old > "$destdir$man1dir/emacs-remote-client$2"
  make -C "$builddir/remote-client" install DESTDIR="$destdir" \
       GZIP_PROG="$1" > "$work/log" 2>&1 \
    || { cat "$work/log" >&2; fail "compress: make failed for GZIP_PROG=[$1]"; }
  [ "$(client_pages "$destdir")" = "./emacs-remote-client$2" ] \
    || fail "compress: GZIP_PROG=[$1] gives the pages" \
	    "[$(client_pages "$destdir")], not emacs-remote-client$2"
  (cd "$destdir$man1dir" \
     && case $2 in *.gz) gzip -dc emacs-remote-client.1.gz ;;
	           *) cat emacs-remote-client.1 ;; esac) \
    | cmp -s - "$page" || fail "compress: GZIP_PROG=[$1]: not the text of $page"
  make -C "$builddir/remote-client" uninstall DESTDIR="$destdir" \
       GZIP_PROG="$1" > "$work/log" 2>&1 \
    || { cat "$work/log" >&2; fail "compress: the uninstall failed"; }
  [ -z "$(client_pages "$destdir")" ] \
    || fail "compress: the uninstall keeps $(client_pages "$destdir")"
}

main "$@"
