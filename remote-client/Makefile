# Build the client of the remote display.

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

# The top-level build of Emacs does not use this file.  The client
# compiles the codec of the server, ../src/rdproto.c.  The directory
# "." comes first in the include path, so <config.h> is the file of
# the client and not a config.h of an Emacs build in ../src.

PKG_CONFIG = pkg-config
PACKAGES = cairo
CFLAGS = -O2 -g
WARNINGS = -Wall -Wextra
ALL_CFLAGS = $(WARNINGS) -I. -I../src \
  $(shell $(PKG_CONFIG) --cflags $(PACKAGES)) $(CFLAGS)
LIBS = $(shell $(PKG_CONFIG) --libs $(PACKAGES))

RENDER_OBJS = render.o rdproto.o

all: test-render

check: test-render
	./test-render

# Build again with AddressSanitizer and UndefinedBehaviorSanitizer, and
# run the test.
SANITIZE = -fsanitize=address,undefined -fno-sanitize-recover=all
check-sanitize: clean
	$(MAKE) CFLAGS="-O1 -g $(SANITIZE)" LDFLAGS="$(SANITIZE)" check
	$(MAKE) clean

test-render: test-render.o $(RENDER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

rdproto.o: ../src/rdproto.c ../src/rdproto.h config.h
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

render.o test-render.o: render.h ../src/rdproto.h

clean:
	rm -f *.o test-render

.PHONY: all check check-sanitize clean
