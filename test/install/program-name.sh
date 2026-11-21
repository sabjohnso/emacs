#!/bin/bash
# Check the installed name of "emacs" and the directory of its DOC file.

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

# Usage: program-name.sh SRCDIR [TRIALS] [SEED]
#
# SRCDIR is the source directory of Emacs.  The check makes a small
# configure script with autoconf.  The script runs the macros of
# SRCDIR/m4/emacs-program-name.m4 after AC_ARG_PROGRAM, as configure.ac
# does.  Its Makefile has the lines TRANSFORM and EMACS_NAME of
# SRCDIR/Makefile.in.  For each trial, the check runs the script with
# a random --program-prefix, --program-suffix, and
# --program-transform-name, and then runs make.  The laws:
#
# - name: emacs_program_name is the output of
#   "echo PREFIXemacsSUFFIX | sed TRANSFORM".  The options can have
#   "\", "$", and "&", which AC_ARG_PROGRAM doubles for make.
# - make: EMACS_NAME of the Makefile is emacs_program_name.
# - doc: if the name is "emacs", etcdocdir is etcdir.  If not,
#   etcdocdir is etcdir with "-NAME" after it.
# - reject: configure stops with an error if the name is not "emacs"
#   and is empty or has a character that is not a letter, a digit,
#   ".", "_", "+", or "-".  Make and sed cannot use such a name in
#   etcdocdir.
#
# A failure prints the seed and the options of the trial.

main () {
  local srcdir trials=${2:-150} seed=${3:-$$}
  srcdir=$(cd "$1" && pwd) || exit 1
  work=$(mktemp -d) || exit 1
  trap 'rm -rf "$work"' EXIT
  make_configure "$srcdir" || exit 1
  RANDOM=$seed
  local i
  count_emacs=0 count_renamed=0 count_rejected=0
  for ((i = 0; i < trials; i++)); do
    make_case
    check_case || {
      echo "program-name.sh: seed $seed, trial $i: prefix [$prefix]" \
	   "suffix [$suffix] transform [$transform]" >&2
      exit 1
    }
  done
  # A generator that misses a kind of name cannot check its law.
  if [ $count_emacs = 0 ] || [ $count_renamed = 0 ] \
       || [ $count_rejected = 0 ]; then
    echo "program-name.sh: seed $seed: a kind of name has no trial" \
	 "($count_emacs emacs, $count_renamed renamed," \
	 "$count_rejected rejected)" >&2
    exit 1
  fi
  echo "program-name.sh: $trials trials pass ($count_emacs emacs," \
       "$count_renamed renamed, $count_rejected rejected), seed $seed"
}

# Make the small configure script and its Makefile.in in $work, from
# the macros and the Makefile.in of the source directory $1.
make_configure () {
  local srcdir=$1
  cat > "$work/configure.ac" <<EOF
AC_INIT([program-name-test], [1])
m4_include([$srcdir/m4/emacs-program-name.m4])
AC_ARG_PROGRAM
emacs_PROGRAM_NAME
etcdir='\${datadir}/emacs/\${version}/etc'
emacs_ETCDOCDIR
AC_SUBST([emacs_program_name])
AC_SUBST([etcdir])
AC_SUBST([etcdocdir])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
EOF
  {
    printf 'prefix = @prefix@\ndatarootdir = @datarootdir@\n'
    printf 'datadir = @datadir@\nversion = 1\n'
    printf 'etcdir = @etcdir@\netcdocdir = @etcdocdir@\n'
    grep -E '^(TRANSFORM|EMACS_NAME) = ' "$srcdir/Makefile.in"
    printf 'print:\n\t@echo "$(EMACS_NAME)|@emacs_program_name@'
    printf '|$(etcdocdir)|$(etcdir)"\n'
  } > "$work/Makefile.in"
  if [ "$(grep -cE '^(TRANSFORM|EMACS_NAME) = ' "$work/Makefile.in")" != 2 ]
  then
    echo "program-name.sh: Makefile.in has no TRANSFORM or EMACS_NAME" >&2
    return 1
  fi
  (cd "$work" && autoconf) || {
    echo "program-name.sh: autoconf failed" >&2
    return 1
  }
}

# Set prefix, suffix, and transform to random options.  An empty
# option is not given.  A prefix or a suffix cannot have "&" or "\",
# because AC_ARG_PROGRAM puts them in an "s&...&" command of sed.
# The generators set REPLY and do not run in a subshell, because a
# subshell gets a new seed of RANDOM.
make_case () {
  random_word 'gx1-._$+'
  prefix=$REPLY
  random_word 'rmt2-._$+'
  suffix=$REPLY
  transform=
  local n=$((RANDOM % 3)) j
  for ((j = 0; j < n; j++)); do
    random_command
    transform+=${transform:+;}$REPLY
  done
}

# Set REPLY to a word of 1 to 3 characters from the set $1, or to an
# empty word in one of two cases.
random_word () {
  local set=$1 n
  REPLY=
  if [ $((RANDOM % 2)) = 0 ]; then
    n=$((1 + RANDOM % 3))
    while ((n-- > 0)); do REPLY+=${set:$((RANDOM % ${#set})):1}; done
  fi
}

# Set REPLY to a random "s" command of sed.  The replacement can have "\\"
# (one "\"), "\&" (one "&"), "&" (the match), "$", and a back
# reference.
random_command () {
  local regexes=('e' '^e' 's$' 'ma' '\(ma\)' 'x' 'emacs' '[cs]' '.*')
  local pieces=('a' 'Z' '-' '$' '\\' '&' '\&' '_')
  local delimiters=',/|'
  local d=${delimiters:$((RANDOM % 3)):1}
  local regex=${regexes[$((RANDOM % ${#regexes[@]}))]}
  local replacement= n=$((RANDOM % 4))
  while ((n-- > 0)); do
    replacement+=${pieces[$((RANDOM % ${#pieces[@]}))]}
  done
  case $regex in *'\('*) replacement+='\1' ;; esac
  local flags=
  [ $((RANDOM % 2)) = 0 ] && flags=g
  REPLY=s$d$regex$d$replacement$d$flags
}

# Run configure and make for the current case, and check the laws.
check_case () {
  local expected
  expected=$(printf '%s\n' "${prefix}emacs${suffix}" \
	       | sed "${transform:-s,x,x,}") || return 1
  local options=()
  [ -n "$prefix" ] && options+=("--program-prefix=$prefix")
  [ -n "$suffix" ] && options+=("--program-suffix=$suffix")
  [ -n "$transform" ] && options+=("--program-transform-name=$transform")
  if (cd "$work" && ./configure "${options[@]}" > configure.out 2>&1); then
    if ! valid_name "$expected"; then
      echo "reject: configure accepted the name [$expected]" >&2
      return 1
    fi
  else
    if valid_name "$expected"; then
      echo "reject: configure failed for the name [$expected]:" >&2
      tail -n 3 "$work/configure.out" >&2
      return 1
    fi
    if ! grep -q 'is not a valid name' "$work/configure.out"; then
      echo "reject: configure failed without the message:" >&2
      tail -n 3 "$work/configure.out" >&2
      return 1
    fi
    count_rejected=$((count_rejected + 1))
    return 0
  fi
  local result
  result=$(make -s --no-print-directory -C "$work" print) || return 1
  local make_name=${result%%|*} rest=${result#*|}
  local name=${rest%%|*}
  rest=${rest#*|}
  local etcdocdir=${rest%%|*} etcdir=${rest#*|}
  if [ "$name" != "$expected" ]; then
    echo "name: expected [$expected], got [$name]" >&2
    return 1
  fi
  if [ "$make_name" != "$name" ]; then
    echo "make: EMACS_NAME is [$make_name], configure has [$name]" >&2
    return 1
  fi
  local expected_doc
  if [ "$name" = emacs ]; then
    expected_doc=$etcdir
    count_emacs=$((count_emacs + 1))
  else
    expected_doc=$etcdir-$name
    count_renamed=$((count_renamed + 1))
  fi
  if [ "$etcdocdir" != "$expected_doc" ]; then
    echo "doc: name [$name], etcdocdir [$etcdocdir]," \
	 "expected [$expected_doc]" >&2
    return 1
  fi
}

# Return true if configure must accept the name $1.
valid_name () {
  case $1 in
    emacs) return 0 ;;
    '' | *[!A-Za-z0-9._+-]*) return 1 ;;
    *) return 0 ;;
  esac
}

main "$@"
