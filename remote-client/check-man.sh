#!/bin/sh
# Check that the man page and the help of the program agree.

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

# Usage: check-man.sh PROGRAM MANPAGE
#
# PROGRAM is the name of the program in the current directory, and
# MANPAGE is its man page.  The facts:
#
# - options: the options in the section "The following OPTIONS are
#   accepted" of "PROGRAM --help" are the options of the labels of the
#   section OPTIONS of MANPAGE, with the same values ("--timeout=MS").
# - lint: "groff -man -ww -z" (or "mandoc -T lint" if groff is not
#   there) gives no warning for MANPAGE.

program=$1
page=$2
status=0

fail () {
  echo "check-man.sh: $*" >&2
  status=1
}

# Print the options of the help, one on each line, sorted.
help_options () {
  ./"$program" --help \
    | sed -n '/^The following OPTIONS are accepted:$/,/^$/p' \
    | grep '^-' | sed 's/  .*//; s/, /\n/g' | sort
}

# Print the options of the labels of the section OPTIONS of the man
# page, one on each line, sorted.
page_options () {
  sed -n '/^\.SH OPTIONS$/,/^\.SH /p' "$page" \
    | sed -n 's/^\.B //p' | sed 's/\\-/-/g; s/, /\n/g' | sort
}

[ -f "$page" ] || { echo "check-man.sh: $page does not exist" >&2; exit 1; }
help=$(help_options)
man=$(page_options)
[ -n "$help" ] || fail "the help has no options"
if [ "$help" != "$man" ]; then
  fail "the options of the help and of $page are not the same:"
  printf '%s\n' "$help" > check-man.help
  printf '%s\n' "$man" > check-man.page
  diff check-man.help check-man.page >&2
  rm -f check-man.help check-man.page
fi

if command -v groff > /dev/null; then
  warnings=$(groff -man -ww -z "$page" 2>&1)
  linter="groff -man -ww -z"
elif command -v mandoc > /dev/null; then
  warnings=$(mandoc -T lint "$page" 2>&1)
  linter="mandoc -T lint"
else
  fail "no groff or mandoc to check $page"
fi
[ -z "$warnings" ] || fail "$linter: $warnings"

exit $status
