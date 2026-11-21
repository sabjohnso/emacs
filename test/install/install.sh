#!/bin/bash
# Check the install of a renamed build with the remote display.

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

# Usage: install.sh BUILDDIR
#
# BUILDDIR is a build that "make" made, with the client program of the
# remote display (HAVE_REMOTE_CLIENT is "yes").  For another build,
# the check prints SKIP and exits with the status 0.
#
# The check installs the build in a temporary DESTDIR, and removes the
# DESTDIR at the end.  These are the facts that it checks in each
# build:
#
# - fingerprint, dump: the build has a fingerprint, and the install
#   puts its dump file in archlibdir.
# - built: "make" made the client program in BUILDDIR/remote-client.
# - missing: the install of a client program that does not exist
#   fails, and tells the user to run "make".
# - client: "make install" puts the client program in bindir, with the
#   name emacs-remote-client.
# - uninstall: "make uninstall" removes the client program and the
#   program of Emacs, and keeps a file of another program in bindir.
#
# A build with the name "emacs" also checks this fact:
#
# - doc: etc/DOC is the DOC file of the build.
#
# A build with another name (for example from --program-suffix=-remote)
# shares the prefix with the "emacs" of another configuration.  The
# DESTDIR first gets the files of that "emacs": an etc/DOC file with
# random bytes, a dump file, and a program in bindir.  The check then
# also checks these facts:
#
# - name: etcdocdir is etcdir with "-NAME" after it.
# - doc: the install keeps the bytes of the planted etc/DOC.  The DOC
#   file of the build goes in etcdocdir.
# - tar: when tar fails in the copy of etc, the install fails, and
#   etc/DOC keeps its bytes.  One pass fails the creation of the
#   archive, and one pass fails its extraction.
# - uninstall: "make uninstall" also removes etcdocdir and the dump
#   file.  It keeps the files that the builds share (emacs-module.h,
#   the manuals, lisp, etc/DOC, and the programs in archlibdir), and
#   the files of the other "emacs".
# - default: with etcdocdir equal to etcdir (as for the name "emacs"),
#   install-arch-indep deletes etc/DOC, and install-etcdoc puts the
#   DOC file of the build there.

main () {
  builddir=$(cd "$1" && pwd) || exit 1
  read_variables
  case $have_remote_client in
    yes | no) ;;
    *) fail "cannot read HAVE_REMOTE_CLIENT: [$have_remote_client]" ;;
  esac
  if [ "$have_remote_client" != yes ]; then
    echo "SKIP install.sh: the build does not make the client program"
    exit 0
  fi
  destdir=$(mktemp -d) || exit 1
  trap 'chmod -R u+w "$destdir"; rm -rf "$destdir"' EXIT
  log=$destdir.log
  check fingerprint [ -n "$fingerprint" ]
  check built [ -x "$builddir/remote-client/$client" ]
  check missing missing_client_fails
  if [ "$emacs_name" = emacs ]; then
    check_default_name
  else
    check_other_name
  fi
  rm -f "$log"
  echo "install.sh: all checks pass (program name $emacs_name)"
}

# Check the install and the uninstall of a build with the name "emacs".
check_default_name () {
  plant_other_program
  install_and_check
  check doc cmp -s "$builddir/etc/DOC" "$destdir$etcdir/DOC"
  uninstall_and_check
}

# Check the install and the uninstall of a build with another name, in
# a prefix that has the files of another "emacs".
check_other_name () {
  check name [ "$etcdocdir" = "$etcdir-$emacs_name" ]
  plant_other_emacs
  install_and_check
  check doc same_as_planted
  check doc [ -f "$destdir$etcdocdir/DOC" ]
  for mode in c x; do
    check "tar -$mode" install_with_failed_tar $mode
    check "tar -$mode" [ -f "$destdir/tar-failed-$mode" ]
    check "tar -$mode" same_as_planted
  done
  uninstall_and_check
  check uninstall [ ! -e "$destdir$etcdocdir" ]
  check uninstall [ ! -e "$destdir$archlibdir/emacs-$fingerprint.pdmp" ]
  check uninstall [ -f "$destdir$includedir/emacs-module.h" ]
  check uninstall info_stays
  check uninstall [ -f "$destdir$lispdir/subdirs.el" ]
  check uninstall [ -x "$destdir$archlibdir/hexl" ]
  check uninstall same_as_planted
  check uninstall [ -e "$destdir$bindir/emacs" ]
  check uninstall [ -e "$destdir$archlibdir/emacs-other.pdmp" ]
  check default install_default_etc
  check default [ ! -e "$destdir$etcdir/DOC" ]
  check default install_default_doc
  check default cmp -s "$builddir/etc/DOC" "$destdir$etcdir/DOC"
}

# Install the build, and check the files that each build installs.
install_and_check () {
  install_quiet || fail "make install failed (see $log)"
  check client [ -x "$destdir$bindir/$client" ]
  check client [ "$client" = emacs-remote-client ]
  check dump [ -f "$destdir$archlibdir/emacs-$fingerprint.pdmp" ]
}

# Uninstall the build, and check the files that each uninstall
# removes or keeps.
uninstall_and_check () {
  make -C "$builddir" uninstall DESTDIR="$destdir" >> "$log" 2>&1 \
    || fail "make uninstall failed (see $log)"
  check uninstall [ ! -e "$destdir$bindir/$client" ]
  check uninstall [ ! -e "$destdir$bindir/$emacs_name" ]
  check uninstall [ -e "$destdir$bindir/other-program" ]
}

# Set the variables of the Makefile of the build that the check uses.
read_variables () {
  have_remote_client=$(variable HAVE_REMOTE_CLIENT)
  emacs_name=$(variable EMACS_NAME)
  bindir=$(variable bindir)
  lispdir=$(variable lispdir)
  etcdir=$(variable etcdir)
  etcdocdir=$(variable etcdocdir)
  archlibdir=$(variable archlibdir)
  includedir=$(variable includedir)
  infodir=$(variable infodir)
  client=$(variable PROGRAM remote-client)
  fingerprint=$("$builddir/src/emacs" --fingerprint 2> /dev/null)
}

# Print the value of the variable $1 in the Makefile of the
# subdirectory $2 of the build, or of the top directory.
variable () {
  printf 'print-variable:\n\t@echo $(%s)\n' "$1" \
    | make -s --no-print-directory -C "$builddir/${2:-.}" -f Makefile -f - \
	   print-variable
}

# Run the check NAME (the first argument) with the command in the other
# arguments.
check () {
  local name=$1
  shift
  "$@" && return 0
  fail "$name: the check \"$*\" failed"
}

fail () {
  echo "FAIL install.sh: $*" >&2
  exit 1
}

install_quiet () {
  make -C "$builddir" install DESTDIR="$destdir" >> "$log" 2>&1
}

# Put a file of another program in bindir of the DESTDIR.
plant_other_program () {
  mkdir -p "$destdir$bindir"
  : > "$destdir$bindir/other-program"
}

# Put the files of the "emacs" of another configuration in the
# DESTDIR: etc/DOC with random bytes (and a copy), a dump file, and the
# program.  Also put a file of another program in bindir.
plant_other_emacs () {
  plant_other_program
  mkdir -p "$destdir$etcdir" "$destdir$archlibdir"
  head -c 4096 /dev/urandom > "$destdir/DOC.planted"
  cp "$destdir/DOC.planted" "$destdir$etcdir/DOC"
  : > "$destdir$archlibdir/emacs-other.pdmp"
  : > "$destdir$bindir/emacs"
}

# Return true if the manual of Emacs is in infodir.
info_stays () {
  local file
  for file in "$destdir$infodir"/emacs.info*; do
    [ -f "$file" ] && return 0
  done
  return 1
}

same_as_planted () {
  cmp -s "$destdir/DOC.planted" "$destdir$etcdir/DOC"
}

# Return true if the install of a client program that does not exist
# fails with a message that tells the user to run "make".
missing_client_fails () {
  local output
  output=$(make -C "$builddir/remote-client" install DESTDIR="$destdir" \
	     PROGRAM=no-such-program 2>&1) && return 1
  case $output in
    *'no-such-program does not exist: run "make"'*) return 0 ;;
    *) echo "$output" >&2; return 1 ;;
  esac
}

# Return true if "make install" fails when tar fails in the copy of
# etc.  If $1 is "c", the fake tar fails when it makes an archive; if
# $1 is "x", it fails when it extracts one.  Before it fails, it makes
# the file tar-failed-$1 in the DESTDIR, so the check can see that the
# install failed because of the fake tar.  It runs the real tar in
# other cases.
install_with_failed_tar () {
  local bin=$destdir/fake-bin-$1 real
  real=$(command -v tar) || return 1
  mkdir -p "$bin" || return 1
  cat > "$bin/tar" <<EOF
#!/bin/sh
case \$PWD in
  */etc) case \$1 in -$1*) : > "$destdir/tar-failed-$1"; exit 1 ;; esac ;;
esac
exec "$real" "\$@"
EOF
  chmod +x "$bin/tar"
  ! PATH=$bin:$PATH install_quiet
}

# Copy etc as a build with the name "emacs" does: etcdocdir is etcdir.
# Only install-arch-indep runs; "-o" stops make from making the targets
# that it depends on.  It must not keep etc/DOC.
install_default_etc () {
  make -C "$builddir" install-arch-indep DESTDIR="$destdir" \
       etcdocdir="$etcdir" -o lisp -o install-info -o install-man \
       -o install-etc >> "$log" 2>&1
}

# Install the DOC file as a build with the name "emacs" does.  Then
# etc/DOC must be the DOC file of the build.
install_default_doc () {
  make -C "$builddir" install-etcdoc DESTDIR="$destdir" \
       etcdocdir="$etcdir" -o src -o install-arch-indep >> "$log" 2>&1
}

main "$@"
