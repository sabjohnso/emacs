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
# - labels: the modes, the options, and the environment variables of
#   "PROGRAM --help" are the labels of the sections DESCRIPTION,
#   OPTIONS, and ENVIRONMENT of MANPAGE, with the same values
#   ("--timeout=MS").
# - defaults: MANPAGE has the defaults of link.h and listenspec.h.
# - lint: "groff -man -ww -z" (or "mandoc -T lint" if groff is not
#   there) gives no warning for MANPAGE.  With neither program, the
#   check prints that it skips the lint.

program=$1
page=$2
srcdir=$(dirname "$0")
status=0
work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM

fail () {
  echo "check-man.sh: $*" >&2
  status=1
}

# Print the labels of the part of the help that starts with the line
# $1, one on each line, sorted.
help_labels () {
  sed -n "/^$1\$/,/^\$/p" "$work/help" | sed '1d' \
    | grep '^[^ ]' | sed 's/  .*//; s/, /\
/g' | sort
}

# Print the labels of the section $1 of the man page, one on each line,
# sorted.  A label is the line ".B LABEL" after ".TP".
page_labels () {
  sed -n "/^\\.SH $1\$/,/^\\.SH /p" "$page" \
    | sed -n '/^\.TP$/{n;s/^\.B //p;}' | sed 's/\\-/-/g; s/, /\
/g' | sort
}

# Check that the part $1 of the help and the section $2 of the page
# have the same labels.
check_labels () {
  help_labels "$1" > "$work/in-help"
  page_labels "$2" > "$work/in-page"
  [ -s "$work/in-help" ] || fail "the help has no \"$1\""
  diff "$work/in-help" "$work/in-page" > "$work/diff" || {
    fail "\"$1\" of the help and $2 of $page are not the same:"
    cat "$work/diff" >&2
  }
}

# Print the value of the constant $1 of the header $2.
constant () {
  sed -n "s/.*[{ ]$1 = \\([0-9][0-9]*\\).*/\\1/p" "$srcdir/$2"
}

# Check that the page has the words $1.
check_words () {
  case $1 in *'[]'*) fail "a constant has no value: $1" ;; esac
  tr '\n' ' ' < "$page" | sed 's/\\-/-/g' | grep -qF -- "$1" \
    || fail "$page has no \"$1\""
}

[ -f "$page" ] || { echo "check-man.sh: $page does not exist" >&2; exit 1; }
./"$program" --help > "$work/help" || fail "--help fails"

check_labels 'The modes:' DESCRIPTION
check_labels 'The following OPTIONS are accepted:' OPTIONS
check_labels 'The environment:' ENVIRONMENT

timeout=$(constant RC_LINK_DEFAULT_TIMEOUT link.h)
pings=$(constant RC_LINK_PINGS_PER_TIMEOUT link.h)
low=$(constant RC_LISTEN_LOW listenspec.h)
high=$(constant RC_LISTEN_HIGH listenspec.h)
check_words "The default is ${timeout:-[]}, and the minimum is ${pings:-[]}."
check_words "and the ports ${low:-[]}-${high:-[]}."

if command -v groff > /dev/null; then
  linter="groff -man -ww -z"
elif command -v mandoc > /dev/null; then
  linter="mandoc -T lint"
else
  linter=
  echo "check-man.sh: no groff and no mandoc, so the lint is skipped"
fi
if [ -n "$linter" ]; then
  warnings=$($linter "$page" 2>&1)
  [ -z "$warnings" ] || fail "$linter: $warnings"
fi

exit $status
