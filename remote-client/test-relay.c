/* Test the relay of remote-client.

Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

/* The test starts "remote-client --relay [--listen=SPEC] SOCKET" with
   a socket of the test.  Most examples give no --listen, and give the
   loopback address as the server address of $SSH_CONNECTION.  The
   test acts as the session at SOCKET, and as the client of the
   relay.  The relay opens no window, so the test needs no display.

   - Line: the relay writes one valid line, and then closes its
     standard input, standard output, and standard error output, so
     SSH can stop.  The process that the test started exits with the
     status 0.  The relay is in a new session, so a hangup signal to
     the group of that process does not stop the relay.
   - Relay: after a client makes a TLS session with the key of the pin
     of the line, and sends the token, the relay connects to the
     session, and copies the bytes in the two directions.  The test
     sends more bytes than the buffers of the relay and of the kernel
     can hold, so both directions must flow at the same time.
   - Wrong: the relay closes a connection with a wrong token, and does
     not connect to the session.  A next client with the token works.
   - Plain: the relay closes a connection that sends the token with no
     TLS, and does not connect to the session.  GnuTLS waits for more
     bytes after some first bytes, so the close can come only at
     RC_RELAY_TOKEN_LIMIT.
   - Rest: when the token and the next bytes come in one record of TLS,
     the session gets the next bytes.
   - Idle: a connection that sends no token does not stop a next
     client with the token.
   - Split: a client can send the token in two parts.
   - Abandon: a connection that sends a part of the token and then
     closes frees its place in the table of pending connections.
   - Overflow: when RC_PENDING_SIZE connections wait for their
     token, one more connection makes the relay close the oldest.
   - Late: the relay closes a connection that sends no token for
     RC_RELAY_TOKEN_LIMIT milliseconds.
   - Replace: a new client with the token replaces the old client.  The
     relay closes the old client and its connection to the session.
   - Session end: when the session closes the connection, the relay
     closes the client.
   - Closed session: the client sends bytes until the session takes no
     more.  Then the session sends its last bytes, and closes the
     connection with the bytes of the client unread.  The client still
     gets the last bytes of the session, and then the end.
   - Gone: when the session removes its socket and then closes the
     connection, as a session does when it ends, the relay exits.
   - End: when the relay cannot connect to the session, it closes the
     client and exits.
   - Loopback: the relay refuses connections to an address that is
     not the loopback address.
   - Default: with no --listen, the port of the relay is in the
     default range of listenspec.h.
   - Range: the relay takes the first free port of the range of
     --listen.
   - Full: when no port of the range is free, the relay writes no line
     and exits with the status 1.
   - No address: with no --listen and no $SSH_CONNECTION, the relay
     writes no line and exits with the status 1.
   - Bad connection: with no --listen and a $SSH_CONNECTION that is not
     valid, the relay writes no line and exits with the status 1.
   - Bad spec: with a --listen that is not a spec, the relay writes no
     line and exits with the status 1.
   - Selected: with --listen=127.0.0.2:PORT, the relay works at
     127.0.0.2, and refuses connections to PORT at 127.0.0.1.  With
     127.0.0.2 as the server address of $SSH_CONNECTION, the relay
     works at 127.0.0.2.  Each address of 127.0.0.0/8 is a loopback
     address of Linux.
   - Precedence: a valid --listen wins over a $SSH_CONNECTION that is
     not valid.
   - IPv6: the relay works on the IPv6 loopback address.  When the
     computer has no IPv6, this example checks nothing.
   - Missing: when SOCKET is not a socket, the relay writes no line and
     exits with the status 1.
   - Usage: with no SOCKET, the program exits with the status 1, also
     after a --listen.  */

#include <config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "listenspec.h"
#include "pending.h"
#include "relay.h"
#include "relayline.h"
#include "stream.h"
#include "tls.h"

/* The longest wait for an event of the relay, in milliseconds.  */
enum { LIMIT = 5000 };

/* The shortest wait that shows that an event does not occur.  */
enum { QUIET = 300 };

/* The number of bytes that the relay example sends.  */
enum { BULK = 16 << 20 };

/* One run of the relay: the directory and the socket of the session,
   the listening socket of the session, the line of the relay, and the
   loopback address where the relay listens, in a listen spec.  The
   range of the spec is not used.  */
struct run
{
  char directory[64];
  char socket[100];
  int session;
  struct rc_relay_line line;
  struct rc_listen_spec at;
};

/* Where a relay listens: the text of --listen, or NULL for none, and
   the value of $SSH_CONNECTION, or NULL for none.  */
struct where
{
  const char *listen;
  const char *connection;
};

/* The place of most examples.  */
static const struct where DEFAULT_WHERE =
  { NULL, "127.0.0.2 40000 127.0.0.1 22" };

static bool line_example (void);
static bool relay_example (void);
static bool wrong_example (void);
static bool plain_example (void);
static bool rest_example (void);
static bool idle_example (void);
static bool late_example (void);
static bool split_example (void);
static bool abandon_example (void);
static bool overflow_example (void);
static bool replace_example (void);
static bool session_end_example (void);
static bool closed_session_example (void);
static bool fill_toward_session (struct rc_stream *);
static bool reads_then_end (struct rc_stream *, const char *, size_t);
static bool gone_example (void);
static bool end_example (void);
static bool loopback_example (void);
static bool default_example (void);
static bool range_example (void);
static bool full_example (void);
static bool no_address_example (void);
static bool bad_connection_example (void);
static bool selected_example (void);
static bool precedence_example (void);
static bool bad_spec_example (void);
static bool ipv6_example (void);
static bool missing_example (void);
static bool usage_example (void);
static bool start_run (struct run *);
static bool start_run_at (struct run *, struct where, const char *);
static bool stop_run (struct run *);
static bool refuses_p (const struct run *);
static bool make_session (struct run *);
static bool refused_p (bool, struct where);
static int start_relay (const char *, struct where, int *, int *, int *);
static int hold_port (uint16_t *);
static bool free_port_p (uint16_t);
static int connect_run (const struct run *);
static int connect_idle (const struct run *);
static bool works_p (const struct run *);
static bool write_closed_p (int);
static bool read_line (int, char *, size_t);
static struct rc_stream *connect_client (const struct run *, bool);
static struct rc_stream *connect_tls (const struct run *);
static bool stream_closed_p (struct rc_stream *, int);
static bool write_stream (struct rc_stream *, const void *, size_t);
static int connect_to_port (struct in_addr, uint16_t);
static int connect_to (const struct sockaddr *, socklen_t);
static int accept_session (const struct run *, int);
static bool closed_p (int, int);
static bool echo_through (struct rc_stream *, int, size_t);
static bool other_address (struct in_addr *);
static int wait_for_exit (pid_t);
static int64_t now_ms (void);

/* An example and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} examples[] =
  {
    { "line", line_example },
    { "relay", relay_example },
    { "wrong", wrong_example },
    { "plain", plain_example },
    { "rest", rest_example },
    { "idle", idle_example },
    { "late", late_example },
    { "split", split_example },
    { "abandon", abandon_example },
    { "overflow", overflow_example },
    { "replace", replace_example },
    { "session end", session_end_example },
    { "closed session", closed_session_example },
    { "gone", gone_example },
    { "end", end_example },
    { "loopback", loopback_example },
    { "default", default_example },
    { "range", range_example },
    { "full", full_example },
    { "no address", no_address_example },
    { "bad connection", bad_connection_example },
    { "selected", selected_example },
    { "precedence", precedence_example },
    { "bad spec", bad_spec_example },
    { "ipv6", ipv6_example },
    { "missing", missing_example },
    { "usage", usage_example },
  };

/* Each example has its own relay, port, and directory, so the
   examples run at the same time, each in its own process.  */
int
main (void)
{
  enum { COUNT = sizeof examples / sizeof *examples };
  pid_t pids[COUNT];
  signal (SIGPIPE, SIG_IGN);
  for (size_t i = 0; i < COUNT; i++)
    {
      pids[i] = fork ();
      if (pids[i] == 0)
	_exit (examples[i].check () ? 0 : 1);
    }
  int failed = 0;
  for (size_t i = 0; i < COUNT; i++)
    {
      int status;
      if (pids[i] < 0 || waitpid (pids[i], &status, 0) != pids[i]
	  || !WIFEXITED (status) || WEXITSTATUS (status) != 0)
	{
	  fprintf (stderr, "test-relay: the %s example failed\n",
		   examples[i].name);
	  failed++;
	}
    }
  if (failed)
    return 1;
  printf ("test-relay: %d examples passed\n", COUNT);
  return 0;
}

static bool
line_example (void)
{
  struct run run;
  return start_run (&run) && stop_run (&run);
}

static bool
relay_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  struct rc_stream *client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  bool ok = client && session >= 0 && echo_through (client, session, BULK);
  rc_stream_close (client);
  close (session);
  return stop_run (&run) && ok;
}

static bool
wrong_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  struct rc_stream *wrong = connect_client (&run, false);
  bool ok = (wrong && stream_closed_p (wrong, LIMIT)
	     && accept_session (&run, QUIET) < 0);
  struct rc_stream *client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  ok = ok && client && session >= 0 && echo_through (client, session, 1000);
  rc_stream_close (wrong);
  rc_stream_close (client);
  close (session);
  return stop_run (&run) && ok;
}

static bool
plain_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  int plain = connect_idle (&run);
  bool ok = (plain >= 0
	     && write (plain, run.line.token, RC_TOKEN_SIZE) == RC_TOKEN_SIZE
	     && closed_p (plain, RC_RELAY_TOKEN_LIMIT + LIMIT)
	     && accept_session (&run, QUIET) < 0);
  close (plain);
  return stop_run (&run) && ok;
}

static bool
rest_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  unsigned char bytes[RC_TOKEN_SIZE + 100], got[100];
  memcpy (bytes, run.line.token, RC_TOKEN_SIZE);
  for (int i = 0; i < 100; i++)
    bytes[RC_TOKEN_SIZE + i] = i;
  struct rc_stream *client = connect_tls (&run);
  bool ok = client && write_stream (client, bytes, sizeof bytes);
  int session = accept_session (&run, LIMIT);
  struct pollfd ready = { session, POLLIN, 0 };
  ok = (ok && session >= 0 && poll (&ready, 1, LIMIT) == 1
	&& read (session, got, sizeof got) == sizeof got
	&& memcmp (got, bytes + RC_TOKEN_SIZE, sizeof got) == 0);
  rc_stream_close (client);
  close (session);
  return stop_run (&run) && ok;
}

static bool
idle_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  int idle = connect_idle (&run);
  struct rc_stream *client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  bool ok = (idle >= 0 && client && session >= 0
	     && echo_through (client, session, 1000));
  close (idle);
  rc_stream_close (client);
  close (session);
  return stop_run (&run) && ok;
}

static bool
late_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  int64_t start = now_ms ();
  int idle = connect_idle (&run);
  bool ok = (idle >= 0 && closed_p (idle, RC_RELAY_TOKEN_LIMIT + LIMIT)
	     && now_ms () - start >= RC_RELAY_TOKEN_LIMIT - QUIET);
  close (idle);
  return stop_run (&run) && ok;
}

static bool
split_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  struct rc_stream *client = connect_tls (&run);
  bool ok = (client
	     && write_stream (client, run.line.token, 10)
	     && poll (NULL, 0, QUIET) == 0
	     && write_stream (client, run.line.token + 10,
			      RC_TOKEN_SIZE - 10));
  int session = accept_session (&run, LIMIT);
  ok = ok && session >= 0 && echo_through (client, session, 1000);
  rc_stream_close (client);
  close (session);
  return stop_run (&run) && ok;
}

static bool
abandon_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  int idle[RC_PENDING_SIZE];
  for (int i = 0; i < RC_PENDING_SIZE - 1; i++)
    idle[i] = connect_idle (&run);
  struct rc_stream *part = connect_tls (&run);
  bool ok = part && write_stream (part, run.line.token, 10);
  rc_stream_close (part);
  poll (NULL, 0, QUIET);
  idle[RC_PENDING_SIZE - 1] = connect_idle (&run);
  ok = ok && !closed_p (idle[0], QUIET) && works_p (&run);
  for (int i = 0; i < RC_PENDING_SIZE; i++)
    close (idle[i]);
  return stop_run (&run) && ok;
}

static bool
overflow_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  int idle[RC_PENDING_SIZE + 1];
  for (int i = 0; i <= RC_PENDING_SIZE; i++)
    {
      idle[i] = connect_idle (&run);
      poll (NULL, 0, 20);
    }
  bool ok = (closed_p (idle[0], RC_RELAY_TOKEN_LIMIT / 2)
	     && !closed_p (idle[RC_PENDING_SIZE], QUIET)
	     && works_p (&run));
  for (int i = 0; i <= RC_PENDING_SIZE; i++)
    close (idle[i]);
  return stop_run (&run) && ok;
}

static bool
replace_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  struct rc_stream *old_client = connect_client (&run, true);
  int old_session = accept_session (&run, LIMIT);
  struct rc_stream *client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  bool ok = (old_client && client && old_session >= 0 && session >= 0
	     && stream_closed_p (old_client, LIMIT)
	     && closed_p (old_session, LIMIT)
	     && echo_through (client, session, 1000));
  rc_stream_close (old_client);
  close (old_session);
  rc_stream_close (client);
  close (session);
  return stop_run (&run) && ok;
}

static bool
session_end_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  struct rc_stream *client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  close (session);
  bool ok = client && session >= 0 && stream_closed_p (client, LIMIT);
  rc_stream_close (client);
  return stop_run (&run) && ok;
}

static bool
closed_session_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  static const char last[] = "the last bytes of the session";
  struct rc_stream *client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  bool ok = (client && session >= 0 && fill_toward_session (client)
	     && write (session, last, sizeof last) == sizeof last);
  if (session >= 0)
    close (session);
  ok = ok && reads_then_end (client, last, sizeof last);
  rc_stream_close (client);
  return stop_run (&run) && ok;
}

/* Write bytes to CLIENT until the relay takes no more for QUIET
   milliseconds, because the session reads nothing.  Return false
   after an error, or if the relay still takes bytes after LIMIT.  */
static bool
fill_toward_session (struct rc_stream *client)
{
  static const unsigned char bytes[4096];
  int64_t start = now_ms ();
  while (now_ms () - start < 4 * LIMIT)
    {
      ptrdiff_t count = rc_stream_write (client, bytes, sizeof bytes);
      if (count == RC_STREAM_FAIL)
	return false;
      struct pollfd ready = { rc_stream_fd (client),
			      rc_stream_events (client, POLLOUT), 0 };
      if (count == RC_STREAM_WAIT && poll (&ready, 1, QUIET) == 0)
	return true;
    }
  return false;
}

/* Return true if the next bytes of CLIENT are the SIZE bytes EXPECTED,
   and then the stream ends.  Wait at most LIMIT for each.  */
static bool
reads_then_end (struct rc_stream *client, const char *expected, size_t size)
{
  char got[256];
  size_t have = 0;
  int64_t start = now_ms ();
  while (have < size && have < sizeof got && now_ms () - start < LIMIT)
    {
      ptrdiff_t count = rc_stream_read (client, got + have,
					sizeof got - have);
      if (count == 0 || count == RC_STREAM_FAIL)
	return false;
      if (count > 0)
	have += count;
      else
	{
	  struct pollfd ready = { rc_stream_fd (client),
				  rc_stream_events (client, POLLIN), 0 };
	  poll (&ready, 1, LIMIT);
	}
    }
  return (have == size && memcmp (got, expected, size) == 0
	  && stream_closed_p (client, LIMIT));
}

static bool
gone_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  struct rc_stream *client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  unlink (run.socket);
  close (session);
  bool ok = (client && session >= 0 && stream_closed_p (client, LIMIT)
	     && refuses_p (&run));
  rc_stream_close (client);
  return stop_run (&run) && ok;
}

/* stop_run is the end example: each other example uses it.  This
   example also checks that a client of a relay with no session gets
   nothing.  */
static bool
end_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  unlink (run.socket);
  struct rc_stream *client = connect_client (&run, true);
  bool ok = client && stream_closed_p (client, LIMIT);
  rc_stream_close (client);
  return stop_run (&run) && ok;
}

static bool
loopback_example (void)
{
  struct in_addr address;
  if (!other_address (&address))
    {
      fprintf (stderr, "test-relay: no address other than loopback; "
	       "the loopback example checks nothing\n");
      return true;
    }
  struct run run;
  if (!start_run (&run))
    return false;
  int fd = connect_to_port (address, run.line.port);
  bool ok = fd < 0;
  if (fd >= 0)
    close (fd);
  return stop_run (&run) && ok;
}

static bool
default_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  bool ok = (run.line.port >= RC_LISTEN_LOW
	     && run.line.port <= RC_LISTEN_HIGH);
  return stop_run (&run) && ok;
}

static bool
range_example (void)
{
  uint16_t port;
  int held = hold_port (&port);
  if (held < 0)
    return false;
  char listen[RC_LISTEN_TEXT_SIZE + sizeof "--listen="];
  snprintf (listen, sizeof listen, "--listen=127.0.0.1:%u-%u",
	    (unsigned) port, (unsigned) port + 1);
  struct run run;
  bool started = start_run_at (&run, (struct where) { listen, NULL },
			       "127.0.0.1");
  close (held);
  return started && stop_run (&run) && run.line.port == port + 1;
}

static bool
full_example (void)
{
  uint16_t port;
  int held = hold_port (&port);
  if (held < 0)
    return false;
  char listen[RC_LISTEN_TEXT_SIZE + sizeof "--listen="];
  snprintf (listen, sizeof listen, "--listen=127.0.0.1:%u",
	    (unsigned) port);
  bool ok = refused_p (false, (struct where) { listen, NULL });
  close (held);
  return ok;
}

static bool
no_address_example (void)
{
  return refused_p (false, (struct where) { NULL, NULL });
}

static bool
bad_connection_example (void)
{
  return refused_p (false,
		    (struct where) { NULL, "127.0.0.2 40000 workstation 22" });
}

static bool
selected_example (void)
{
  /* Other examples listen on ports of the default range at 127.0.0.1
     at the same time, so the refused connection needs a port that
     they do not use.  */
  uint16_t port;
  int held = hold_port (&port);
  if (held < 0)
    return false;
  close (held);
  char listen[64];
  snprintf (listen, sizeof listen, "--listen=127.0.0.2:%u", (unsigned) port);
  struct run run;
  if (!start_run_at (&run, (struct where) { listen, NULL }, "127.0.0.2"))
    return false;
  int fd = connect_to_port ((struct in_addr) { htonl (INADDR_LOOPBACK) },
			    port);
  if (fd >= 0)
    close (fd);
  struct run other;
  return stop_run (&run) && fd < 0
    && start_run_at (&other,
		     (struct where) { NULL, "127.0.0.3 40000 127.0.0.2 22" },
		     "127.0.0.2")
    && stop_run (&other);
}

static bool
precedence_example (void)
{
  struct run run;
  return start_run_at (&run, (struct where) { "--listen=127.0.0.1",
					      "not a connection" },
		       "127.0.0.1")
    && stop_run (&run);
}

static bool
bad_spec_example (void)
{
  return refused_p (false, (struct where) { "--listen=localhost",
					 DEFAULT_WHERE.connection });
}

static bool
ipv6_example (void)
{
  int fd = socket (AF_INET6, SOCK_STREAM, 0);
  struct sockaddr_in6 address = { .sin6_family = AF_INET6,
				  .sin6_addr = IN6ADDR_LOOPBACK_INIT };
  bool v6 = fd >= 0 && bind (fd, (struct sockaddr *) &address,
			     sizeof address) == 0;
  if (fd >= 0)
    close (fd);
  if (!v6)
    {
      fprintf (stderr, "test-relay: no IPv6 loopback address; "
	       "the ipv6 example checks nothing\n");
      return true;
    }
  struct run run;
  return start_run_at (&run, (struct where) { "--listen=::1", NULL }, "::1")
    && stop_run (&run);
}

static bool
missing_example (void)
{
  return refused_p (true, DEFAULT_WHERE);
}

static bool
usage_example (void)
{
  pid_t pid = fork ();
  if (pid == 0)
    {
      int null = open ("/dev/null", O_WRONLY);
      dup2 (null, STDERR_FILENO);
      execl ("./remote-client", "remote-client", "--relay", (char *) NULL);
      _exit (127);
    }
  pid_t listen = fork ();
  if (listen == 0)
    {
      int null = open ("/dev/null", O_WRONLY);
      dup2 (null, STDERR_FILENO);
      execl ("./remote-client", "remote-client", "--relay",
	     "--listen=127.0.0.1", (char *) NULL);
      _exit (127);
    }
  return wait_for_exit (pid) == 1 && wait_for_exit (listen) == 1;
}

/* Make a session socket for RUN, start the relay, and read its line.
   Check that the relay closes its standard streams, that the process
   that the test started exits with the status 0, and that the relay
   lives after a hangup signal to the group of that process.  */
static bool
start_run (struct run *run)
{
  return start_run_at (run, DEFAULT_WHERE, "127.0.0.1");
}

/* Start the run RUN as start_run does, but with the relay at WHERE.
   ADDRESS is the numeric loopback address of WHERE.  */
static bool
start_run_at (struct run *run, struct where where, const char *address)
{
  if (!rc_listen_spec_parse (address, &run->at))
    return false;
  if (!make_session (run))
    return false;
  int in, out, err;
  pid_t pid = start_relay (run->socket, where, &in, &out, &err);
  char text[RC_RELAY_LINE_SIZE];
  bool ok = (read_line (out, text, sizeof text)
	     && rc_relay_line_parse (text, &run->line)
	     && closed_p (out, LIMIT) && closed_p (err, LIMIT)
	     && write_closed_p (in));
  close (in);
  close (out);
  close (err);
  ok = wait_for_exit (pid) == 0 && ok;
  /* The process of the test was the leader of its group.  */
  kill (-pid, SIGHUP);
  return ok && works_p (run);
}

/* Stop the relay of RUN as the end example says: remove the session
   socket, and connect a client.  Check that the relay closes the
   client, or has already exited, and that the port of the relay then refuses connections.
   Remove the directory of RUN.  */
static bool
stop_run (struct run *run)
{
  close (run->session);
  unlink (run->socket);
  rmdir (run->directory);
  struct rc_stream *client = connect_client (run, true);
  bool ok = !client || stream_closed_p (client, LIMIT);
  rc_stream_close (client);
  return refuses_p (run) && ok;
}

/* Return true if the port of the relay of RUN refuses connections in
   LIMIT milliseconds, because the relay exited.  */
static bool
refuses_p (const struct run *run)
{
  for (int64_t start = now_ms (); now_ms () - start < LIMIT;
       poll (NULL, 0, 10))
    {
      int fd = connect_run (run);
      if (fd < 0)
	return true;
      close (fd);
    }
  return false;
}

/* Make the directory and the session socket of RUN.  Return false on
   an error.  */
static bool
make_session (struct run *run)
{
  strcpy (run->directory, "/tmp/test-relay-XXXXXX");
  if (!mkdtemp (run->directory))
    return false;
  snprintf (run->socket, sizeof run->socket, "%s/socket", run->directory);
  run->session = socket (AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  snprintf (address.sun_path, sizeof address.sun_path, "%s", run->socket);
  return run->session >= 0
    && bind (run->session, (struct sockaddr *) &address,
	     sizeof address) == 0
    && listen (run->session, 4) == 0;
}

/* Return true if the relay at WHERE writes no line, and exits with
   the status 1.  With MISSING, the session socket does not exist.
   Otherwise, it is a real socket, so only WHERE can be the cause.  */
static bool
refused_p (bool missing, struct where where)
{
  struct run run;
  if (!make_session (&run))
    return false;
  int in, out, err;
  pid_t pid = start_relay (missing ? "/nonexistent/socket" : run.socket,
			   where, &in, &out, &err);
  char text[RC_RELAY_LINE_SIZE];
  bool ok = !read_line (out, text, sizeof text);
  close (in);
  close (out);
  close (err);
  ok = wait_for_exit (pid) == 1 && ok;
  close (run.session);
  unlink (run.socket);
  rmdir (run.directory);
  return ok;
}

/* Start "remote-client --relay SOCKET" at WHERE as the leader of a new
   group of processes.  Put the write end of its standard input in *IN,
   and the read ends of its standard output and standard error output
   in *OUT and *ERR.  Return its process.  */
static int
start_relay (const char *socket, struct where where, int *in, int *out,
	     int *err)
{
  int in_fds[2], out_fds[2], err_fds[2];
  if (pipe (in_fds) != 0 || pipe (out_fds) != 0 || pipe (err_fds) != 0)
    return -1;
  pid_t pid = fork ();
  if (pid == 0)
    {
      setpgid (0, 0);
      dup2 (in_fds[0], STDIN_FILENO);
      dup2 (out_fds[1], STDOUT_FILENO);
      dup2 (err_fds[1], STDERR_FILENO);
      for (int i = 0; i < 2; i++)
	{
	  close (in_fds[i]);
	  close (out_fds[i]);
	  close (err_fds[i]);
	}
      if (where.connection)
	setenv ("SSH_CONNECTION", where.connection, 1);
      else
	unsetenv ("SSH_CONNECTION");
      if (where.listen)
	execl ("./remote-client", "remote-client", "--relay", where.listen,
	       socket, (char *) NULL);
      else
	execl ("./remote-client", "remote-client", "--relay", socket,
	       (char *) NULL);
      _exit (127);
    }
  setpgid (pid, pid);
  close (in_fds[0]);
  close (out_fds[1]);
  close (err_fds[1]);
  *in = in_fds[1];
  *out = out_fds[0];
  *err = err_fds[0];
  return pid;
}

/* Return true if each reader of the pipe FD closes it in LIMIT
   milliseconds.  */
static bool
write_closed_p (int fd)
{
  struct pollfd ready = { .fd = fd, .events = 0 };
  return poll (&ready, 1, LIMIT) == 1 && (ready.revents & POLLERR);
}

/* Return true if a client with the token of the relay of RUN gets a
   connection to the session, and the bytes go through.  Then close the
   client, and wait until the relay closes its connection to the
   session.  Thus the relay has no pair when this function returns, and
   a next step of an example cannot race with the close.  */
static bool
works_p (const struct run *run)
{
  struct rc_stream *client = connect_client (run, true);
  int session = accept_session (run, LIMIT);
  bool ok = client && session >= 0 && echo_through (client, session, 100);
  rc_stream_close (client);
  ok = ok && closed_p (session, LIMIT);
  close (session);
  return ok;
}

/* Read one line from FD into TEXT, with the newline and a null byte.
   Return false if FD ends before a newline, or the line does not fit
   in SIZE bytes.  */
static bool
read_line (int fd, char *text, size_t size)
{
  for (size_t length = 0; length + 1 < size; length++)
    {
      if (read (fd, text + length, 1) != 1)
	return false;
      if (text[length] == '\n')
	{
	  text[length + 1] = 0;
	  return true;
	}
    }
  return false;
}

/* Connect a client to the relay of RUN with TLS, and send the token
   of the relay if GOOD, or else a wrong token.  Return the stream, or
   NULL.  */
static struct rc_stream *
connect_client (const struct run *run, bool good)
{
  struct rc_stream *stream = connect_tls (run);
  uint8_t token[RC_TOKEN_SIZE];
  memcpy (token, run->line.token, sizeof token);
  if (!good)
    token[RC_TOKEN_SIZE - 1] ^= 1;
  if (stream && !write_stream (stream, token, sizeof token))
    {
      rc_stream_close (stream);
      return NULL;
    }
  return stream;
}

/* Connect a client to the relay of RUN with TLS, and check the pin of
   the line.  Send nothing.  Return the stream, whose socket does not
   block, or NULL.  */
static struct rc_stream *
connect_tls (const struct run *run)
{
  int fd = connect_run (run);
  struct rc_stream *stream
    = fd < 0 ? NULL : rc_tls_connect (fd, run->line.pin, LIMIT);
  if (stream)
    fcntl (fd, F_SETFL, O_NONBLOCK);
  return stream;
}

/* Write the SIZE BYTES to STREAM.  Return false if they do not all go
   in LIMIT milliseconds.  */
static bool
write_stream (struct rc_stream *stream, const void *bytes, size_t size)
{
  const unsigned char *rest = bytes;
  int64_t start = now_ms ();
  while (size > 0)
    {
      ptrdiff_t count = rc_stream_write (stream, rest, size);
      if (count == RC_STREAM_FAIL || now_ms () - start > LIMIT)
	return false;
      if (count > 0)
	{
	  rest += count;
	  size -= count;
	}
      else
	{
	  struct pollfd ready = { rc_stream_fd (stream),
				  rc_stream_events (stream, POLLOUT), 0 };
	  poll (&ready, 1, LIMIT);
	}
    }
  return true;
}

/* Connect a client to the relay of RUN, and send nothing.  Return the
   connection.  */
static int
connect_idle (const struct run *run)
{
  return connect_run (run);
}

/* Return a connection to the relay of RUN at its loopback address, or
   -1.  */
static int
connect_run (const struct run *run)
{
  struct sockaddr_storage address;
  socklen_t size = rc_listen_spec_address (&run->at, run->line.port,
					   &address);
  return connect_to ((struct sockaddr *) &address, size);
}

/* Return a connection to PORT at ADDRESS, or -1.  */
static int
connect_to_port (struct in_addr address, uint16_t port)
{
  struct sockaddr_in socket_address = { .sin_family = AF_INET,
					.sin_port = htons (port),
					.sin_addr = address };
  return connect_to ((struct sockaddr *) &socket_address,
		     sizeof socket_address);
}

/* Return a connection to ADDRESS of SIZE bytes, or -1.  */
static int
connect_to (const struct sockaddr *address, socklen_t size)
{
  int fd = socket (address->sa_family, SOCK_STREAM, 0);
  if (fd >= 0 && connect (fd, address, size) != 0)
    {
      close (fd);
      return -1;
    }
  return fd;
}

/* Listen on a new port of the loopback address whose next port is
   free.  Put the port in *PORT, and return the socket, or -1.  */
static int
hold_port (uint16_t *port)
{
  for (int tries = 0; tries < 100; tries++)
    {
      struct sockaddr_in address = { .sin_family = AF_INET,
				     .sin_addr = { htonl (INADDR_LOOPBACK) } };
      socklen_t size = sizeof address;
      int fd = socket (AF_INET, SOCK_STREAM, 0);
      if (fd < 0)
	return -1;
      if (bind (fd, (struct sockaddr *) &address, sizeof address) == 0
	  && listen (fd, 1) == 0
	  && getsockname (fd, (struct sockaddr *) &address, &size) == 0
	  && ntohs (address.sin_port) < 65535
	  && free_port_p (ntohs (address.sin_port) + 1))
	{
	  *port = ntohs (address.sin_port);
	  return fd;
	}
      close (fd);
    }
  return -1;
}

/* Return true if PORT of the loopback address is free now.  */
static bool
free_port_p (uint16_t port)
{
  struct sockaddr_in address = { .sin_family = AF_INET,
				 .sin_port = htons (port),
				 .sin_addr = { htonl (INADDR_LOOPBACK) } };
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  bool free = fd >= 0 && bind (fd, (struct sockaddr *) &address,
			       sizeof address) == 0;
  if (fd >= 0)
    close (fd);
  return free;
}

/* Return the next connection of the relay to the session of RUN, or -1
   if none comes in WAIT milliseconds.  */
static int
accept_session (const struct run *run, int wait)
{
  struct pollfd ready = { .fd = run->session, .events = POLLIN };
  if (poll (&ready, 1, wait) != 1)
    return -1;
  return accept (run->session, NULL, NULL);
}

/* Return true if the peer closes FD in WAIT milliseconds.  Discard the
   bytes that come before the end.  */
static bool
closed_p (int fd, int wait)
{
  int64_t start = now_ms ();
  for (;;)
    {
      int left = wait - (now_ms () - start);
      struct pollfd ready = { .fd = fd, .events = POLLIN };
      if (left <= 0 || poll (&ready, 1, left) != 1)
	return false;
      char bytes[4096];
      ssize_t count = read (fd, bytes, sizeof bytes);
      if (count == 0 || (count < 0 && errno == ECONNRESET))
	return true;
      if (count < 0)
	return false;
    }
}

/* Return true if the stream STREAM ends or fails in WAIT
   milliseconds.  Discard the bytes that come before.  */
static bool
stream_closed_p (struct rc_stream *stream, int wait)
{
  int64_t start = now_ms ();
  for (;;)
    {
      char bytes[4096];
      ptrdiff_t count = rc_stream_read (stream, bytes, sizeof bytes);
      if (count == 0 || count == RC_STREAM_FAIL)
	return true;
      int left = wait - (now_ms () - start);
      if (left <= 0)
	return false;
      struct pollfd ready = { rc_stream_fd (stream),
			      rc_stream_events (stream, POLLIN), 0 };
      if (count == RC_STREAM_WAIT && poll (&ready, 1, left) != 1)
	return false;
    }
}

/* Send SIZE bytes on CLIENT.  Act as a session that sends back each
   byte that comes on SESSION.  Return true if CLIENT gets the same
   bytes again.  All the sockets are nonblocking, so the bytes flow in
   the two directions at the same time.

   First the client reads nothing, until no write is possible for
   QUIET milliseconds.  Then each buffer along the path is full, so
   the relay must also write parts of its buffers.  */
static bool
echo_through (struct rc_stream *client, int session, size_t size)
{
  static unsigned char echo[1 << 16];
  size_t sent = 0, got = 0, echo_start = 0, echo_end = 0;
  bool stalled = true;
  fcntl (session, F_SETFL, O_NONBLOCK);
  int64_t start = now_ms ();
  while (got < size)
    {
      if (now_ms () - start > 4 * LIMIT)
	return false;
      bool buffered = !stalled && rc_stream_buffered_p (client);
      struct pollfd fds[2]
	= { { .fd = rc_stream_fd (client),
	      .events = (stalled ? 0 : POLLIN) | (sent < size ? POLLOUT : 0) },
	    { .fd = session,
	      .events = echo_start < echo_end ? POLLOUT : POLLIN } };
      int ready = poll (fds, 2, buffered ? 0 : stalled ? QUIET : LIMIT);
      if (ready < 0 || (ready == 0 && !stalled && !buffered))
	return false;
      if (ready == 0 || sent == size)
	stalled = false;
      if (buffered)
	fds[0].revents |= POLLIN;
      if (fds[0].revents & POLLOUT)
	{
	  unsigned char bytes[4096];
	  size_t count = size - sent < sizeof bytes ? size - sent
						    : sizeof bytes;
	  for (size_t i = 0; i < count; i++)
	    bytes[i] = (sent + i) * 7;
	  ptrdiff_t written = rc_stream_write (client, bytes, count);
	  if (written == RC_STREAM_FAIL)
	    return false;
	  if (written > 0)
	    sent += written;
	}
      if (!stalled && fds[0].revents & (POLLIN | POLLHUP | POLLERR))
	{
	  unsigned char bytes[4096];
	  ptrdiff_t count = rc_stream_read (client, bytes, sizeof bytes);
	  if (count == RC_STREAM_WAIT)
	    continue;
	  if (count <= 0)
	    return false;
	  for (ptrdiff_t i = 0; i < count; i++)
	    if (bytes[i] != (unsigned char) ((got + i) * 7))
	      return false;
	  got += count;
	}
      if (fds[1].revents & POLLOUT)
	{
	  ssize_t written = write (session, echo + echo_start,
				   echo_end - echo_start);
	  if (written > 0 && (echo_start += written) == echo_end)
	    echo_start = echo_end = 0;
	}
      else if (fds[1].revents & (POLLIN | POLLHUP | POLLERR))
	{
	  ssize_t count = read (session, echo, sizeof echo);
	  if (count <= 0)
	    return false;
	  echo_end = count;
	}
    }
  return true;
}

/* Put an IPv4 address of this computer that is not a loopback address
   in ADDRESS.  Return false if the computer has none.  */
static bool
other_address (struct in_addr *address)
{
  struct ifaddrs *list;
  if (getifaddrs (&list) != 0)
    return false;
  bool found = false;
  for (struct ifaddrs *each = list; each && !found; each = each->ifa_next)
    if (each->ifa_addr && each->ifa_addr->sa_family == AF_INET)
      {
	*address = ((struct sockaddr_in *) each->ifa_addr)->sin_addr;
	found = (ntohl (address->s_addr) >> 24) != 127;
      }
  freeifaddrs (list);
  return found;
}

/* Wait for the exit of the process PID, and return its status, or -1
   if it does not exit normally in LIMIT milliseconds.  */
static int
wait_for_exit (pid_t pid)
{
  for (int waited = 0; waited < LIMIT; waited += 10)
    {
      int status;
      if (waitpid (pid, &status, WNOHANG) == pid)
	return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
      poll (NULL, 0, 10);
    }
  kill (pid, SIGKILL);
  waitpid (pid, NULL, 0);
  return -1;
}

/* Return the time of a monotonic clock in milliseconds.  */
static int64_t
now_ms (void)
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
