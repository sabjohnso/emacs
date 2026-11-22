#!/bin/sh
# Check the help, the version, and the usage errors of the program.

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

# Usage: check-usage.sh PROGRAM VERSION
#
# PROGRAM is the name of the program in the current directory, and
# VERSION is the version of Emacs.  The facts:
#
# - help: "PROGRAM --help" and "PROGRAM -H" exit with the status 0,
#   write nothing to the standard error output, and the first line of
#   their output starts with "Usage: PROGRAM ".  The last line is the
#   line of emacsclient about bugs.
# - version: "PROGRAM --version" and "PROGRAM -V" exit with the status
#   0, and print "PROGRAM VERSION".
# - error: with no argument, with an option that does not exist, or
#   with --relay after another option, the program exits with the
#   status 1, and tells the user to run "PROGRAM --help".

program=$1
version=$2
status=0

fail () {
  echo "check-usage.sh: $*" >&2
  status=1
}

# Run the program with the arguments, and set out, err, and code.
run () {
  out=$(./"$program" "$@" 2> check-usage.err)
  code=$?
  err=$(cat check-usage.err)
  rm -f check-usage.err
}

for option in --help -H; do
  run $option
  [ $code = 0 ] || fail "$option: the exit status is $code"
  [ -z "$err" ] || fail "$option: it writes [$err] to the error output"
  case $(printf '%s\n' "$out" | head -n 1) in
    "Usage: $program "*) ;;
    *) fail "$option: the first line is not \"Usage: $program ...\"" ;;
  esac
  [ "$(printf '%s\n' "$out" | tail -n 1)" \
      = "Report bugs with M-x report-emacs-bug." ] \
    || fail "$option: the last line is not the line about bugs"
done

for option in --version -V; do
  run $option
  [ $code = 0 ] || fail "$option: the exit status is $code"
  [ "$out" = "$program $version" ] \
    || fail "$option: it prints [$out], not [$program $version]"
done

for arguments in "" "--no-such-option" "--timeout=100 --relay socket"; do
  run $arguments
  [ $code = 1 ] || fail "[$arguments]: the exit status is $code, not 1"
  case $err in
    *"Try '$program --help' for more information"*) ;;
    *) fail "[$arguments]: the error output [$err] has no hint of --help" ;;
  esac
done

exit $status
