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
# - cases: for each line of the table below, the program exits with the
#   status of the line and writes its words.  The table does not need a
#   display: the program stops before it opens a window, because the
#   socket does not exist or because the SSH program fails.  Each line
#   with --ssh has --ssh-command=false first, so also a program that is
#   wrong connects to no host.

program=$1
version=$2
status=0
errors=$(mktemp) || exit 1
trap 'rm -f "$errors"' EXIT
trap 'exit 1' HUP INT TERM

fail () {
  echo "check-usage.sh: $*" >&2
  status=1
}

# Run the program with the arguments, and set out, err, and code.
run () {
  out=$(./"$program" "$@" 2> "$errors" < /dev/null)
  code=$?
  err=$(cat "$errors")
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

# Each line has the kind, the words, and the arguments.  The kinds:
#
# - usage: the status is 1.  The error output has the words, and the
#   hint of --help.
# - error: the status is 1.  The error output has the words, and no
#   hint.  The words show that the program accepted the arguments and
#   stopped later, or that a value is not valid.
# - ok: the status is 0.  The output has the words.
count=0
while IFS='|' read -r kind words arguments; do
  count=$((count + 1))
  run $arguments
  case $kind in
    ok) expected=0 text=$out ;;
    *) expected=1 text=$err ;;
  esac
  [ $code = $expected ] \
    || fail "[$arguments]: the exit status is $code, not $expected"
  case $text in
    *"$words"*) ;;
    *) fail "[$arguments]: the output [$text] has no [$words]" ;;
  esac
  case $err in
    *"Try '$program --help' for more information"*) hint=usage ;;
    *) hint= ;;
  esac
  case $kind$hint in
    usageusage | error | ok) ;;
    *) fail "[$arguments]: the hint of --help: [$hint] for $kind" ;;
  esac
done <<EOF
usage|SOCKET or --ssh HOST is necessary|
usage|--no-such-option: not an option|--no-such-option
usage|-x: not an option|-x
usage|-h: not an option|-h
usage|-Hx: not an option|-Hx socket
usage|--timeout: not an option|--timeout 100 socket
usage|--help=x: not an option|--help=x
usage|2: not a valid timeout|--timeout=2 ./no-socket
usage|abc: not a valid timeout|--timeout=abc ./no-socket
usage|3x: not a valid timeout|--timeout=3x ./no-socket
usage|: not a valid timeout|--timeout= ./no-socket
usage|--ssh needs HOST|--ssh-command=false --ssh
usage|--ssh HOST|--ssh-command=false --ssh=host
usage|socket: --ssh takes no SOCKET|--ssh-command=false --ssh host socket
usage|--render-to is not valid with --ssh|--ssh-command=false --render-to=x.png --ssh host
usage|b: only one SOCKET is valid|./a b
usage|need --ssh|--listen=127.0.0.1 ./no-socket
usage|need --ssh|--connect=127.0.0.1 ./no-socket
usage|need --ssh|--session=remote-2 ./no-socket
usage|--relay must be the first argument|--timeout=100 --relay ./no-socket
usage|--relay needs [--listen=SPEC] SOCKET|--relay
usage|--relay needs [--listen=SPEC] SOCKET|--relay --listen=127.0.0.1
usage|--relay needs [--listen=SPEC] SOCKET|--relay --timeout=100 ./no-socket
usage|--relay needs [--listen=SPEC] SOCKET|--relay ./a ./b
error|localhost: not a listen spec|--ssh-command=false --listen=localhost --ssh host
error|a_b: not a name of a session|--ssh-command=false --session=a_b --ssh host
error|localhost: not a listen spec|--relay --listen=localhost ./no-socket
error|./no-socket: not a socket|--relay --listen=127.0.0.1 ./no-socket
error|-: not a socket|--relay --listen=127.0.0.1 -
error|./no-socket: No such file or directory|./no-socket
error|./no-socket: No such file or directory|--timeout=3 ./no-socket
error|./no-socket: No such file or directory|--render-to=x.png ./no-socket
error|-: No such file or directory|-
error|host: |--ssh-command=false --connect=127.0.0.1 --ssh host
error|host: |--ssh-command=false --connect=127.0.0.1 --listen=127.0.0.1 --session=a-1 --timeout=3 --ssh host
ok|Usage: $program|--timeout=100 --help
ok|$program $version|--timeout=100 -V
EOF
[ $count = 37 ] || fail "the table has $count cases, not 37"

exit $status
