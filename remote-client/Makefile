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

# The relay, the program, and the tests of TLS use GnuTLS.
TLS_CFLAGS = $(shell $(PKG_CONFIG) --cflags gnutls)
TLS_LIBS = $(shell $(PKG_CONFIG) --libs gnutls)

# Only the program uses GTK.  The modules and their tests do not.
GTK_CFLAGS = $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS = $(shell $(PKG_CONFIG) --libs gtk+-3.0)

# test-reconnect asks a window of the program to close through X, as a
# window manager does.
X11_LIBS = $(shell $(PKG_CONFIG) --libs x11)

RENDER_OBJS = render.o table.o rdproto.o
READER_OBJS = reader.o rdproto.o
FONTS_OBJS = fonts.o hashfonts.o hashcache.o
CLIENT_OBJS = client.o view.o input.o sizes.o reader.o render.o table.o \
  fonts.o hashfonts.o hashcache.o rdproto.o link.o backoff.o \
  unixsocket.o relay.o relayline.o token.o flow.o pending.o stream.o tls.o \
  listenspec.o \
  sshargs.o sshconfig.o bootstrap.o direct.o clock.o child.o
TESTS = test-table test-render test-reader test-glyphs test-fonts \
  test-input test-client test-sizes test-view test-link \
  test-backoff test-reconnect test-relayline \
  test-relay test-flow test-pending test-unixsocket \
  test-sshargs test-ssh test-tls test-listenspec test-sshconfig

# The view needs a display.  Xvfb gives the test one, so no window
# comes on the screen of the user.
XVFB_RUN = xvfb-run -a

all: remote-client $(TESTS)

remote-client: $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(GTK_LIBS) $(TLS_LIBS) $(LIBS)

check: remote-client $(TESTS)
	./test-table
	./test-render
	./test-reader
	./test-glyphs
	./test-fonts
	./test-input
	./test-client
	./test-sizes
	./test-link
	./test-backoff
	./test-relayline
	./test-relay
	./test-flow
	./test-pending
	./test-unixsocket
	./test-sshargs
	./test-tls
	./test-listenspec
	./test-sshconfig
	$(XVFB_RUN) ./test-view
	$(XVFB_RUN) ./test-reconnect
	$(XVFB_RUN) ./test-ssh

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

test-reconnect: test-reconnect.o rdproto.o | remote-client
	$(CC) $(LDFLAGS) -o $@ $^ $(X11_LIBS)

test-backoff: test-backoff.o backoff.o
	$(CC) $(LDFLAGS) -o $@ $^

test-relayline: test-relayline.o relayline.o token.o
	$(CC) $(LDFLAGS) -o $@ $^

# The test runs the program, so the program must be current.
test-relay: test-relay.o relayline.o token.o tls.o stream.o listenspec.o \
  | remote-client
	$(CC) $(LDFLAGS) -o $@ $^ $(TLS_LIBS)

test-flow: test-flow.o flow.o
	$(CC) $(LDFLAGS) -o $@ $^

test-pending: test-pending.o pending.o token.o
	$(CC) $(LDFLAGS) -o $@ $^

test-unixsocket: test-unixsocket.o unixsocket.o
	$(CC) $(LDFLAGS) -o $@ $^

test-sshargs: test-sshargs.o sshargs.o listenspec.o
	$(CC) $(LDFLAGS) -o $@ $^

test-ssh: test-ssh.o rdproto.o relayline.o token.o tls.o stream.o \
  | remote-client
	$(CC) $(LDFLAGS) -o $@ $^ $(TLS_LIBS)

test-sshconfig: test-sshconfig.o sshconfig.o
	$(CC) $(LDFLAGS) -o $@ $^

test-listenspec: test-listenspec.o listenspec.o
	$(CC) $(LDFLAGS) -o $@ $^

test-tls: test-tls.o tls.o stream.o
	$(CC) $(LDFLAGS) -o $@ $^ $(TLS_LIBS)

test-link: test-link.o link.o
	$(CC) $(LDFLAGS) -o $@ $^

test-view: test-view.o view.o input.o sizes.o $(RENDER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(GTK_LIBS) $(LIBS)

client.o view.o input.o test-input.o test-view.o: %.o: %.c
	$(CC) $(ALL_CFLAGS) $(GTK_CFLAGS) -c -o $@ $<

tls.o test-tls.o test-relay.o test-ssh.o: %.o: %.c
	$(CC) $(ALL_CFLAGS) $(TLS_CFLAGS) -c -o $@ $<

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
link.o test-link.o client.o: link.h
backoff.o test-backoff.o test-reconnect.o client.o: backoff.h
test-reconnect.o: ../src/rdproto.h
relayline.o test-relayline.o test-relay.o relay.o: relayline.h token.h pin.h
token.o: token.h
relay.o client.o test-relay.o: relay.h
flow.o test-flow.o relay.o: flow.h
pending.o test-pending.o relay.o test-relay.o: pending.h token.h
unixsocket.o client.o relay.o test-unixsocket.o: unixsocket.h
client.o: connector.h
sshargs.o test-sshargs.o bootstrap.o: sshargs.h listenspec.h
bootstrap.o client.o: bootstrap.h relayline.h token.h listenspec.h sshconfig.h
direct.o client.o: direct.h relayline.h token.h stream.h
clock.o relay.o bootstrap.o: clock.h
child.o bootstrap.o: child.h
stream.o relay.o client.o direct.o test-ssh.o: stream.h
stream.o tls.o: streamclass.h
tls.o test-tls.o test-relay.o test-ssh.o relay.o direct.o: tls.h pin.h stream.h
test-ssh.o: relayline.h token.h ../src/rdproto.h
listenspec.o test-listenspec.o relay.o client.o test-relay.o: listenspec.h
sshconfig.o test-sshconfig.o bootstrap.o client.o: sshconfig.h

clean:
	rm -f *.o remote-client $(TESTS)

.PHONY: all check check-sanitize clean
