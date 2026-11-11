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
PACKAGES = cairo cairo-ft fontconfig freetype2 glib-2.0
CFLAGS = -O2 -g
WARNINGS = -Wall -Wextra
ALL_CFLAGS = $(WARNINGS) -I. -I../src \
  $(shell $(PKG_CONFIG) --cflags $(PACKAGES)) $(CFLAGS)
LIBS = $(shell $(PKG_CONFIG) --libs $(PACKAGES)) -lm

# Only the program uses GTK.  The modules and their tests do not.
GTK_CFLAGS = $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS = $(shell $(PKG_CONFIG) --libs gtk+-3.0)

RENDER_OBJS = render.o table.o rdproto.o
READER_OBJS = reader.o rdproto.o
FONTS_OBJS = fonts.o hashfonts.o hashcache.o
CLIENT_OBJS = client.o view.o input.o sizes.o reader.o render.o table.o \
  fonts.o hashfonts.o hashcache.o rdproto.o
TESTS = test-table test-render test-reader test-glyphs test-fonts \
  test-input test-client test-sizes test-view

# The view needs a display.  Xvfb gives the test one, so no window
# comes on the screen of the user.
XVFB_RUN = xvfb-run -a

all: remote-client $(TESTS)

remote-client: $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(GTK_LIBS) $(LIBS)

check: remote-client $(TESTS)
	./test-table
	./test-render
	./test-reader
	./test-glyphs
	./test-fonts
	./test-input
	./test-client
	./test-sizes
	$(XVFB_RUN) ./test-view

# Build again with AddressSanitizer and UndefinedBehaviorSanitizer, and
# run the test.
SANITIZE = -fsanitize=address,undefined -fno-sanitize-recover=all
check-sanitize: clean
	LSAN_OPTIONS=suppressions=$(CURDIR)/lsan.supp \
	  $(MAKE) CFLAGS="-O1 -g $(SANITIZE)" LDFLAGS="$(SANITIZE)" check
	$(MAKE) clean

test-render: test-render.o $(RENDER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

test-table: test-table.o table.o
	$(CC) $(LDFLAGS) -o $@ $^

test-reader: test-reader.o $(READER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

test-glyphs: test-glyphs.o $(RENDER_OBJS) $(FONTS_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

test-fonts: test-fonts.o $(RENDER_OBJS) $(FONTS_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

test-input: test-input.o input.o rdproto.o
	$(CC) $(LDFLAGS) -o $@ $^ $(GTK_LIBS) $(LIBS)

test-client: test-client.o $(RENDER_OBJS) $(FONTS_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

test-sizes: test-sizes.o sizes.o
	$(CC) $(LDFLAGS) -o $@ $^

test-view: test-view.o view.o input.o sizes.o $(RENDER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(GTK_LIBS) $(LIBS)

client.o view.o input.o test-input.o test-view.o: %.o: %.c
	$(CC) $(ALL_CFLAGS) $(GTK_CFLAGS) -c -o $@ $<

rdproto.o: ../src/rdproto.c ../src/rdproto.h config.h
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

render.o test-render.o: render.h table.h ../src/rdproto.h
table.o test-table.o: table.h
reader.o test-reader.o: reader.h ../src/rdproto.h
fonts.o test-glyphs.o test-client.o test-fonts.o: fonts.h render.h \
  ../src/rdproto.h
hashfonts.o test-fonts.o client.o: hashfonts.h render.h ../src/rdproto.h
hashfonts.o hashcache.o client.o: hashcache.h ../src/rdproto.h
client.o view.o test-view.o: view.h reader.h fonts.h render.h \
  ../src/rdproto.h
input.o test-input.o view.o: input.h ../src/rdproto.h
sizes.o test-sizes.o view.o: sizes.h

clean:
	rm -f *.o remote-client $(TESTS)

.PHONY: all check check-sanitize clean
