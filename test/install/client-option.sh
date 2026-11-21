#!/bin/bash
# Check when configure makes the client program of the remote display.

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

# Usage: client-option.sh SRCDIR BUILDDIR
#
# SRCDIR is the source directory of Emacs, and BUILDDIR is a configured
# build of it.  The check makes a small configure script with autoconf.
# The script runs the macro emacs_REMOTE_CLIENT of
# SRCDIR/m4/emacs-remote-client.m4, with EMACS_CHECK_MODULES from
# SRCDIR/configure.ac (and the macros of m4/pkg.m4 and m4/ndk-build.m4
# that it uses).  Before the macro, it runs the lines of configure.ac
# that set HAVE_REMOTE_DISPLAY: the option --with-remote-display
# (OPTION_DEFAULT_OFF), the window system "remote", and the block of
# HAVE_REMOTE_DISPLAY.  The check runs the script for
# each combination of the options and the libraries below, and
# compares the result with expected_result.  Each run of configure has
# an empty PKG_CONFIG_PATH, so the environment of the caller cannot
# change what pkg-config finds.
#
# The check also makes sure that the real build files use the result.
# configure.ac must call emacs_REMOTE_CLIENT after it sets
# HAVE_REMOTE_DISPLAY, and show HAVE_REMOTE_CLIENT in its summary.  In
# the Makefile of BUILDDIR, with HAVE_REMOTE_CLIENT "yes" and then
# "no", actual-all must depend on
# remote-client, install-arch-dep must install the client, and
# uninstall must uninstall it, only for "yes".

# The check runs each combination of these values.  A library value
# other than "all" names a package that pkg-config does not find when
# configure asks for it (see hide_package).
client_options=('' '--with-remote-client' '--with-remote-client=yes'
		'--without-remote-client' '--with-remote-client=no'
		'--with-remote-client=maybe')
display_options=('' '--with-remote-display' '--with-remote-display=no')
libraries=(all gtk+-3.0 gnutls cairo x11)

# Print the expected result for the client option $1, the display
# option $2, and the libraries $3: "yes" or "no" for
# HAVE_REMOTE_CLIENT, or "error:KIND" if configure must stop.  KIND is
# "value" (not a value of the option), "conflict" (the remote display
# without the client), or "package" (a missing library).  Only the
# tests of the client use x11.
expected_result () {
  local missing=yes display=
  case $3 in all | x11) missing= ;; esac
  [ "$2" = --with-remote-display ] && display=yes
  case $1 in
    --with-remote-client=maybe) echo error:value ;;
    --without-remote-client | --with-remote-client=no)
      if [ -n "$display" ]; then echo error:conflict; else echo no; fi ;;
    *)
      if [ -z "$missing" ]; then
	echo yes
      elif [ -n "$display" ] || [ -n "$1" ]; then
	echo error:package
      else
	echo no
      fi ;;
  esac
}

# Print a phrase that the error message of the kind $1 has, and that
# the messages of the other kinds do not have.
error_phrase () {
  case $1 in
    value) echo 'is not a valid value of --with-remote-client' ;;
    conflict) echo 'needs the client program' ;;
    package) echo 'pkg-config packages cairo' ;;
  esac
}

main () {
  if [ $# != 2 ]; then
    echo "Usage: client-option.sh SRCDIR BUILDDIR" >&2
    exit 2
  fi
  local srcdir
  srcdir=$(cd "$1" && pwd) || exit 1
  local failed=0
  check_configure_ac "$srcdir" || failed=1
  check_makefile "$2" || failed=1
  work=$(mktemp -d) || exit 1
  trap 'rm -rf "$work"' EXIT
  make_configure "$srcdir" || exit 1
  local library
  for library in "${libraries[@]}"; do
    [ "$library" = all ] || hide_package "$library" || exit 1
  done
  local client display count=0
  for client in "${client_options[@]}"; do
    for display in "${display_options[@]}"; do
      for library in "${libraries[@]}"; do
	check_case "$client" "$display" "$library" || failed=1
	count=$((count + 1))
      done
    done
  done
  [ $failed = 0 ] || exit 1
  echo "client-option.sh: $count cases pass"
}

# Check the real configure.ac of the source directory $1.
check_configure_ac () {
  awk '
    /^AC_SUBST\(\[HAVE_REMOTE_DISPLAY\]\)$/ { display = NR }
    /^ *emacs_REMOTE_CLIENT$/ { if (display) client = NR }
    /HAVE_REMOTE_CLIENT}$/ { if (client) summary = NR }
    END { exit !(display && client && summary) }
  ' "$1/configure.ac" || {
    echo "client-option.sh: configure.ac does not call emacs_REMOTE_CLIENT" \
	 "after HAVE_REMOTE_DISPLAY, or does not show HAVE_REMOTE_CLIENT" >&2
    return 1
  }
}

# Check the Makefile of the configured build $1 with HAVE_REMOTE_CLIENT
# "yes" and "no".  "make -p" prints the rules after make reads the
# Makefile, so each "ifeq" has its effect.  The goal is a target that
# does not exist, so make runs no command, also no recursive make.
# "-o Makefile" stops make from making the Makefile again.
check_makefile () {
  local value failed=0 database
  for value in yes no; do
    database=$(make -s --no-print-directory -C "$1" -pq -o Makefile \
		 HAVE_REMOTE_CLIENT=$value client-option-no-target 2> /dev/null)
    check_rule "$database" actual-all '^actual-all:.* remote-client( |$)' \
	       $value || failed=1
    check_rule "$database" install-arch-dep \
	       '^	\$\(MAKE\) -C remote-client install$' $value || failed=1
    check_rule "$database" uninstall \
	       '^	\$\(MAKE\) -C remote-client uninstall$' $value || failed=1
  done
  return $failed
}

# Check that the rule $2 in the make database $1 has a line that
# matches the extended regular expression $3 if $4 is "yes", and has
# no such line if $4 is "no".
check_rule () {
  local found=no
  printf '%s\n' "$1" \
    | awk -v target="$2" '
	index($0, target ":") == 1 { inside = 1 }
	inside && /^$/ { exit }
	inside { print }' \
    | grep -Eq "$3" && found=yes
  if [ "$found" != "$4" ]; then
    echo "FAIL client-option.sh: with HAVE_REMOTE_CLIENT=$4, the rule" \
	 "$2 of the Makefile has a match of [$3]: $found" >&2
    return 1
  fi
}

# Make the small configure script in $work.
make_configure () {
  local srcdir=$1
  {
    echo 'AC_INIT([client-option-test], [1])'
    echo "m4_include([$srcdir/m4/pkg.m4])"
    echo "m4_include([$srcdir/m4/ndk-build.m4])"
    echo "m4_include([$srcdir/m4/emacs-remote-client.m4])"
    # The definition of EMACS_CHECK_MODULES in configure.ac.
    sed -n '/^AC_DEFUN(\[EMACS_CHECK_MODULES\],$/,/^$/p' \
	"$srcdir/configure.ac"
    echo 'PKG_PROG_PKG_CONFIG'
    display_lines "$srcdir/configure.ac" || return 1
    echo 'emacs_REMOTE_CLIENT'
    echo 'echo "result: HAVE_REMOTE_CLIENT=$HAVE_REMOTE_CLIENT"'
    echo 'AC_OUTPUT'
  } > "$work/configure.ac"
  grep -q '^AC_DEFUN(\[EMACS_CHECK_MODULES\],$' "$work/configure.ac" || {
    echo "client-option.sh: configure.ac has no EMACS_CHECK_MODULES" >&2
    return 1
  }
  (cd "$work" && autoconf) || {
    echo "client-option.sh: autoconf failed" >&2
    return 1
  }
}

# Print the lines of the configure.ac $1 that set HAVE_REMOTE_DISPLAY:
# the definition and the call of OPTION_DEFAULT_OFF for remote-display,
# the "if" that sets the window system "remote", and the block from
# HAVE_REMOTE_DISPLAY=no to its AC_SUBST.  Return false if a part is
# missing.
display_lines () {
  local part text
  for part in \
      '/^AC_DEFUN(\[OPTION_DEFAULT_OFF\], \[dnl$/,/^\])dnl$/' \
      '/^OPTION_DEFAULT_OFF(\[remote-display\],$/,/^$/' \
      '/^if test "\${with_remote_display}" = "yes"; then$/,/^fi$/' \
      '/^HAVE_REMOTE_DISPLAY=no$/,/^AC_SUBST(\[HAVE_REMOTE_DISPLAY\])$/'
  do
    text=$(sed -n "${part}p" "$1")
    if [ -z "$text" ]; then
      echo "client-option.sh: configure.ac has no part $part" >&2
      return 1
    fi
    printf '%s\n' "$text"
  done
}

# Make $work/no-$1, a pkg-config that fails when the modules that it
# gets have the package $1, and runs the real pkg-config in other
# cases.  A .pc file that is not there cannot hide only x11, because
# other packages of the client need x11.
hide_package () {
  local real
  real=$(command -v pkg-config) || return 1
  cat > "$work/no-$1" <<EOF
#!/bin/sh
for argument in "\$@"; do
  case " \$argument " in *" $1 "*) exit 1 ;; esac
done
exec "$real" "\$@"
EOF
  chmod +x "$work/no-$1"
  PKG_CONFIG_PATH= pkg-config --exists "$1" || {
    echo "client-option.sh: pkg-config does not find $1" >&2
    return 1
  }
}

# Run the configure script with the client option $1, the display
# option $2, and the libraries $3, and check its result.
check_case () {
  local options="$1 $2" expected
  expected=$(expected_result "$1" "$2" "$3")
  local output status
  if [ "$3" = all ]; then
    output=$(cd "$work" && PKG_CONFIG_PATH= ./configure $options 2>&1)
  else
    output=$(cd "$work" && PKG_CONFIG_PATH= PKG_CONFIG=$work/no-$3 \
	       ./configure $options 2>&1)
  fi
  status=$?
  local actual
  if [ $status != 0 ]; then
    actual=error
  else
    actual=$(printf '%s\n' "$output" \
	       | sed -n 's/^result: HAVE_REMOTE_CLIENT=//p')
  fi
  if [ "$actual" != "${expected%%:*}" ]; then
    echo "FAIL client-option.sh: options [$options], hidden [$3]:" \
	 "expected [$expected], got [$actual]" >&2
    printf '%s\n' "$output" | tail -n 4 >&2
    return 1
  fi
  if [ "$actual" = error ]; then
    local phrase
    phrase=$(error_phrase "${expected#error:}")
    # A package error also names the package that is missing.
    if [ "$expected" = error:package ] \
       && ! printf '%s\n' "$output" | grep -qF -- "$3"; then
      phrase=$3
    fi
    if ! printf '%s\n' "$output" | grep -qF -- "$phrase"; then
      echo "FAIL client-option.sh: options [$options], hidden [$3]: the" \
	   "error has no [$phrase]" >&2
      printf '%s\n' "$output" | tail -n 4 >&2
      return 1
    fi
  fi
}

main "$@"
