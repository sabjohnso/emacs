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
   - End: when the relay cannot connect to the session, it sends the
     client an end-session message with the reason RD_END_NO_SESSION,
     closes the client, and exits.  Thus the client knows that the
     session does not exist, and does not connect again.
   - Refuse two: the client of the end example gets the end message,
     but does not close.  While the relay waits for that close, a next
     client with the token also gets the end message and then the end.
     Then the relay exits.
   - Return: while the relay waits for the close of a refused client,
     the session comes back, and a next client with the token connects
     to it.  The relay keeps that pair after the refused client closes,
     and does not exit.
   - Pending refusal: a next client makes its TLS session while the
     relay refuses a client, and sends its token only after the refused
     client closed.  The relay does not exit before that token, and the
     next client also gets the end message and then the end.
   - Model: random sequences of 2 to 12 steps.  A step takes the
     session down or up, connects a client with the token, connects a
     client with its TLS session but no token, sends the token of the
     oldest such client, closes the refused client, or closes the
     client of the working pair.  After each step, and at the end, the
     relay agrees with a reference model of its stop rule: it refuses a
     client while the session is down, and the bytes of a client reach
     a session that is up.  At the end, each waiting client sends its
     token, and the relay exits if and only if the model stops.  The
     trials run in parallel processes, at most MODEL_PARALLEL at a
     time.  The number of trials is $TEST_RELAY_TRIALS, or
     MODEL_TRIALS.  Each run also has the trial of the seed
     11294005353004319, which found a defect.  A failed trial writes
     its seed, and "test-relay SEED" runs that trial only.
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
#include "rdproto.h"
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
static bool refuse_two_example (void);
static bool pending_refusal_example (void);
static bool model_example (void);
static bool model_trial (uint64_t);
static bool parse_seed (const char *, uint64_t *);
static bool return_example (void);
static bool listen_session (struct run *);
static bool reads_bytes (struct rc_stream *, const char *, size_t);
static size_t end_bytes (char *);
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
    { "refuse two", refuse_two_example },
    { "return", return_example },
    { "pending refusal", pending_refusal_example },
    { "model", model_example },
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
main (int argc, char **argv)
{
  enum { COUNT = sizeof examples / sizeof *examples };
  if (argc == 2)
    {
      uint64_t seed;
      if (!parse_seed (argv[1], &seed))
	{
	  fprintf (stderr, "test-relay: %s: not a seed\n", argv[1]);
	  return 2;
	}
      signal (SIGPIPE, SIG_IGN);
      return model_trial (seed) ? 0 : 1;
    }
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
  return (reads_bytes (client, expected, size)
	  && stream_closed_p (client, LIMIT));
}

/* Return true if the next bytes of CLIENT are the SIZE bytes EXPECTED.
   Wait at most LIMIT.  */
static bool
reads_bytes (struct rc_stream *client, const char *expected, size_t size)
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
  return have == size && memcmp (got, expected, size) == 0;
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
   only the end-session message of a session that does not exist.  */
static bool
end_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  unlink (run.socket);
  char expected[16];
  size_t size = end_bytes (expected);
  struct rc_stream *client = connect_client (&run, true);
  bool ok = client && reads_then_end (client, expected, size);
  rc_stream_close (client);
  return stop_run (&run) && ok;
}

static bool
refuse_two_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  unlink (run.socket);
  char expected[16];
  size_t size = end_bytes (expected);
  struct rc_stream *first = connect_client (&run, true);
  bool ok = first && reads_bytes (first, expected, size);
  struct rc_stream *second = ok ? connect_client (&run, true) : NULL;
  ok = ok && second && reads_then_end (second, expected, size)
    && refuses_p (&run);
  rc_stream_close (first);
  rc_stream_close (second);
  return stop_run (&run) && ok;
}

static bool
return_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  close (run.session);
  unlink (run.socket);
  char expected[16];
  size_t size = end_bytes (expected);
  struct rc_stream *refused = connect_client (&run, true);
  bool ok = refused && reads_bytes (refused, expected, size)
    && listen_session (&run);
  struct rc_stream *client = ok ? connect_client (&run, true) : NULL;
  int session = client ? accept_session (&run, LIMIT) : -1;
  rc_stream_close (refused);
  static const char bytes[] = "after the refusal";
  poll (NULL, 0, QUIET);
  ok = ok && session >= 0 && write_stream (client, bytes, sizeof bytes);
  struct pollfd ready = { .fd = session, .events = POLLIN };
  char got[sizeof bytes];
  ok = (ok && poll (&ready, 1, LIMIT) == 1
	&& read (session, got, sizeof got) == sizeof got
	&& memcmp (got, bytes, sizeof got) == 0);
  rc_stream_close (client);
  if (session >= 0)
    close (session);
  return stop_run (&run) && ok;
}

static bool
pending_refusal_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  unlink (run.socket);
  char expected[16];
  size_t size = end_bytes (expected);
  struct rc_stream *refused = connect_client (&run, true);
  bool ok = refused && reads_bytes (refused, expected, size);
  struct rc_stream *next = ok ? connect_tls (&run) : NULL;
  rc_stream_close (refused);
  poll (NULL, 0, QUIET);
  ok = (ok && next && write_stream (next, run.line.token, RC_TOKEN_SIZE)
	&& reads_then_end (next, expected, size));
  rc_stream_close (next);
  return stop_run (&run) && ok;
}

/* The model example.  MODEL_TRIALS is the default number of trials
   with a seed from the clock, and MODEL_PARALLEL the most trials that
   run at the same time.  A trial has MODEL_MIN_STEPS to MODEL_STEPS
   random steps.  MODEL_SEED found a defect, so each run checks it.  */
enum { MODEL_TRIALS = 50, MODEL_PARALLEL = 8,
       MODEL_MIN_STEPS = 2, MODEL_STEPS = 12 };
static const uint64_t MODEL_SEED = 11294005353004319u;

/* The longest time of a refusal, in milliseconds, after which a trial
   makes no more random steps.  The relay closes a refused client after
   END_LIMIT (1000) milliseconds of relay.c, and the model has no
   clock.  */
enum { MODEL_REFUSAL_MARGIN = 600 };

static bool model_trial_count (int *);
static bool run_trials (const uint64_t *, int);

static bool
model_example (void)
{
  int count;
  if (!model_trial_count (&count))
    return false;
  uint64_t *seeds = malloc ((count + 1) * sizeof *seeds);
  if (!seeds)
    return false;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * MODEL_TRIALS;
  seeds[0] = MODEL_SEED;
  for (int trial = 0; trial < count; trial++)
    seeds[trial + 1] = base + trial;
  bool ok = run_trials (seeds, count + 1);
  free (seeds);
  return ok;
}

/* Put the number of trials of the model example in *COUNT: the value of
   $TEST_RELAY_TRIALS, or MODEL_TRIALS.  Return false with a message if
   the value is not a number from 0 to 100000.  */
static bool
model_trial_count (int *count)
{
  const char *text = getenv ("TEST_RELAY_TRIALS");
  if (!text)
    {
      *count = MODEL_TRIALS;
      return true;
    }
  char *end;
  long value = strtol (text, &end, 10);
  if (end == text || *end || value < 0 || value > 100000)
    {
      fprintf (stderr, "test-relay: TEST_RELAY_TRIALS=%s: not a number of "
	       "trials\n", text);
      return false;
    }
  *count = value;
  return true;
}

/* Run the trial of each of the COUNT SEEDS in its own process, at most
   MODEL_PARALLEL at the same time.  Write the seed of each trial that
   fails.  Return true if all pass.  */
static bool
run_trials (const uint64_t *seeds, int count)
{
  struct { pid_t pid; uint64_t seed; } running[MODEL_PARALLEL];
  int nrunning = 0, next = 0;
  bool ok = true;
  while (next < count || nrunning > 0)
    {
      if (next < count && nrunning < MODEL_PARALLEL)
	{
	  pid_t pid = fork ();
	  if (pid == 0)
	    _exit (model_trial (seeds[next]) ? 0 : 1);
	  if (pid < 0)
	    return false;
	  running[nrunning].pid = pid;
	  running[nrunning++].seed = seeds[next++];
	  continue;
	}
      int status;
      pid_t pid = wait (&status);
      for (int i = 0; i < nrunning; i++)
	if (running[i].pid == pid)
	  {
	    if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
	      {
		fprintf (stderr, "test-relay: the model fails with seed "
			 "%llu\n", (unsigned long long) running[i].seed);
		ok = false;
	      }
	    running[i] = running[--nrunning];
	    break;
	  }
    }
  return ok;
}

/* Put the number TEXT in *SEED.  Return false if TEXT is not a
   decimal number.  */
static bool
parse_seed (const char *text, uint64_t *seed)
{
  char *end;
  errno = 0;
  unsigned long long value = strtoull (text, &end, 10);
  if (end == text || *end || errno != 0 || *text == '-')
    return false;
  *seed = value;
  return true;
}

/* The steps of a trial of the model example.  */
enum model_step
  {
    STEP_DOWN,
    STEP_UP,
    STEP_CONNECT,
    STEP_TLS,
    STEP_TOKEN,
    STEP_CLOSE_REFUSED,
    STEP_CLOSE_PAIR,
    STEP_COUNT,
  };

/* The reference model of the stop rule of the relay.  UP is true while
   the session listens.  PAIR is true while a client is connected to
   the session, and REFUSED while the relay refuses a client.  WAITING
   is the number of clients with a TLS session and no token.  STOPPING
   is true after a refusal, until a pair opens.  The relay exits when it
   stops, has no refused client, and has no waiting client, or when the
   session ends the pair and its socket is gone.  */
struct model
{
  bool up, pair, refused, stopping, exited;
  int waiting;
};

/* The real side of a trial: the run, the client and the session side
   of the working pair, and the refused client.  REFUSED_AT is the time
   of rc_now_ms when the refusal of REFUSED started.  OLD holds the
   refused clients that the relay replaced, and WAITING the clients
   with no token, the oldest first.  */
struct real
{
  struct run run;
  struct rc_stream *pair_client;
  int pair_session;
  struct rc_stream *refused;
  int64_t refused_at;
  struct rc_stream *old[MODEL_STEPS + RC_PENDING_SIZE];
  int nold;
  struct rc_stream *waiting[RC_PENDING_SIZE];
  int nwaiting;
};

/* What a trial sees on the real side after a step.  GOT_END is true if
   the refused client got the end-session message, and PAIR_WORKS if the
   bytes of the client of the pair reach the session.  */
struct observation
{
  bool got_end, pair_works;
};

static enum model_step random_step (const struct model *, uint64_t *);
static bool model_step_valid_p (const struct model *, enum model_step);
static void model_apply (struct model *, enum model_step);
static void model_connect (struct model *);
static enum model_step final_step (const struct model *);
static bool real_apply (struct real *, enum model_step);
static bool real_connect (struct real *, struct rc_stream *);
static bool refusal_young_p (const struct real *);
static struct observation observe_real (struct real *, bool);
static bool pair_works_p (struct real *);
static bool agrees_p (const struct model *, const struct observation *);
static void close_real (struct real *);
static uint64_t next_random (uint64_t *);

/* Run the trial of SEED.  Return true if the relay agrees with the
   model after each step and at the end.  HEARD is true after the
   refused client got the end-session message, so the trial reads the
   message only one time.  */
static bool
model_trial (uint64_t seed)
{
  uint64_t state = seed;
  struct model model = { .up = true };
  struct real real = { .pair_session = -1 };
  if (!start_run (&real.run))
    return false;
  bool ok = true, heard = false;
  int steps = (MODEL_MIN_STEPS
	       + next_random (&state) % (MODEL_STEPS - MODEL_MIN_STEPS + 1));
  for (int i = 0; ok && !model.exited; i++)
    {
      bool random = i < steps && refusal_young_p (&real);
      enum model_step step = (random ? random_step (&model, &state)
			      : final_step (&model));
      if (step == STEP_COUNT)
	break;
      struct rc_stream *refused = real.refused;
      model_apply (&model, step);
      ok = real_apply (&real, step);
      /* A new refused client did not get the message yet.  */
      heard = heard && real.refused == refused;
      struct observation seen = observe_real (&real, heard);
      heard = seen.got_end;
      ok = ok && agrees_p (&model, &seen);
    }
  ok = ok && (model.exited ? refuses_p (&real.run)
	      : (poll (NULL, 0, QUIET), pair_works_p (&real) == model.pair));
  close_real (&real);
  return stop_run (&real.run) && ok;
}

/* Return a random step that can come in the state MODEL.  */
static enum model_step
random_step (const struct model *model, uint64_t *state)
{
  enum model_step step;
  do
    step = next_random (state) % STEP_COUNT;
  while (!model_step_valid_p (model, step));
  return step;
}

/* Return the next step of the end of a trial in the state MODEL: the
   token of a waiting client, then the close of the refused client.
   Return STEP_COUNT when the end is complete.  */
static enum model_step
final_step (const struct model *model)
{
  return (model->waiting > 0 ? STEP_TOKEN
	  : model->refused ? STEP_CLOSE_REFUSED : STEP_COUNT);
}

/* Return true if STEP can come in the state MODEL.  The relay holds at
   most RC_PENDING_SIZE waiting clients.  */
static bool
model_step_valid_p (const struct model *model, enum model_step step)
{
  switch (step)
    {
    case STEP_DOWN:
      return model->up;
    case STEP_UP:
      return !model->up;
    case STEP_CONNECT:
      return true;
    case STEP_TLS:
      return model->waiting < RC_PENDING_SIZE;
    case STEP_TOKEN:
      return model->waiting > 0;
    case STEP_CLOSE_REFUSED:
      return model->refused;
    case STEP_CLOSE_PAIR:
      return model->pair;
    case STEP_COUNT:
      break;
    }
  return false;
}

/* Change MODEL as the relay changes after STEP.  */
static void
model_apply (struct model *model, enum model_step step)
{
  switch (step)
    {
    case STEP_DOWN:
      /* A session that ends removes its socket, and then closes its
	 connection.  */
      model->up = false;
      model->exited = model->pair;
      model->pair = false;
      break;
    case STEP_UP:
      model->up = true;
      break;
    case STEP_CONNECT:
      model_connect (model);
      break;
    case STEP_TLS:
      model->waiting++;
      break;
    case STEP_TOKEN:
      model->waiting--;
      model_connect (model);
      break;
    case STEP_CLOSE_REFUSED:
      model->refused = false;
      model->exited = model->stopping && model->waiting == 0;
      break;
    case STEP_CLOSE_PAIR:
      model->pair = false;
      break;
    case STEP_COUNT:
      break;
    }
}

/* Change MODEL as the relay changes after a client sends its token:
   the client becomes the pair if the session is up, and else the
   refused client.  */
static void
model_connect (struct model *model)
{
  model->pair = model->up;
  if (model->up)
    model->stopping = false;
  else
    model->refused = model->stopping = true;
}

/* Do STEP on the real side REAL.  Return false on an error of the
   test.  */
static bool
real_apply (struct real *real, enum model_step step)
{
  struct run *run = &real->run;
  switch (step)
    {
    case STEP_DOWN:
      close (run->session);
      run->session = -1;
      unlink (run->socket);
      if (real->pair_session >= 0)
	close (real->pair_session);
      real->pair_session = -1;
      rc_stream_close (real->pair_client);
      real->pair_client = NULL;
      return true;
    case STEP_UP:
      return listen_session (run);
    case STEP_CONNECT:
      return real_connect (real, connect_client (run, true));
    case STEP_TLS:
      {
	struct rc_stream *client = connect_tls (run);
	real->waiting[real->nwaiting++] = client;
	return client != NULL;
      }
    case STEP_TOKEN:
      {
	struct rc_stream *client = real->waiting[0];
	memmove (real->waiting, real->waiting + 1,
		 --real->nwaiting * sizeof *real->waiting);
	if (!write_stream (client, run->line.token, RC_TOKEN_SIZE))
	  {
	    rc_stream_close (client);
	    return false;
	  }
	return real_connect (real, client);
      }
    case STEP_CLOSE_REFUSED:
      rc_stream_close (real->refused);
      real->refused = NULL;
      return true;
    case STEP_CLOSE_PAIR:
      rc_stream_close (real->pair_client);
      real->pair_client = NULL;
      {
	bool closed = closed_p (real->pair_session, LIMIT);
	close (real->pair_session);
	real->pair_session = -1;
	return closed;
      }
    case STEP_COUNT:
      break;
    }
  return false;
}

/* Make CLIENT, which sent its token, the pair of REAL if the session is
   up, or else its refused client.  Return false if CLIENT is NULL, or
   if the session gets no connection.  */
static bool
real_connect (struct real *real, struct rc_stream *client)
{
  struct run *run = &real->run;
  if (!client)
    return false;
  if (run->session < 0)
    {
      /* The relay closes the refused client before CLIENT.  */
      if (real->refused)
	real->old[real->nold++] = real->refused;
      real->refused = client;
      real->refused_at = now_ms ();
      return true;
    }
  rc_stream_close (real->pair_client);
  if (real->pair_session >= 0)
    close (real->pair_session);
  real->pair_client = client;
  real->pair_session = accept_session (run, LIMIT);
  return real->pair_session >= 0;
}

/* Return true if REAL has no refused client, or if its refusal is
   younger than MODEL_REFUSAL_MARGIN milliseconds.  */
static bool
refusal_young_p (const struct real *real)
{
  return (!real->refused
	  || now_ms () - real->refused_at < MODEL_REFUSAL_MARGIN);
}

/* Return what REAL shows now.  HEARD is true if the refused client got
   the end-session message before: then this function does not read
   it again.  Read the message of a refused client that did not get it
   yet, and send bytes through the pair.  */
static struct observation
observe_real (struct real *real, bool heard)
{
  char expected[16];
  size_t size = end_bytes (expected);
  bool got_end = (real->refused
		  && (heard || reads_bytes (real->refused, expected, size)));
  return (struct observation) { got_end, pair_works_p (real) };
}

/* Return true if REAL has a pair, and the bytes of its client reach the
   session.  */
static bool
pair_works_p (struct real *real)
{
  if (!real->pair_client)
    return false;
  static const char bytes[] = "pair";
  struct pollfd ready = { .fd = real->pair_session, .events = POLLIN };
  char got[sizeof bytes];
  return (write_stream (real->pair_client, bytes, sizeof bytes)
	  && poll (&ready, 1, LIMIT) == 1
	  && read (real->pair_session, got, sizeof got) == sizeof got
	  && memcmp (got, bytes, sizeof got) == 0);
}

/* Return true if SEEN is what MODEL says: a refused client gets the
   end-session message, and the bytes of a pair reach the session.
   After an exit, the trial checks the end only.  */
static bool
agrees_p (const struct model *model, const struct observation *seen)
{
  return (model->exited
	  || (seen->got_end == model->refused
	      && seen->pair_works == model->pair));
}

/* Close each client and the session side of the pair of REAL.  */
static void
close_real (struct real *real)
{
  rc_stream_close (real->pair_client);
  rc_stream_close (real->refused);
  for (int i = 0; i < real->nold; i++)
    rc_stream_close (real->old[i]);
  for (int i = 0; i < real->nwaiting; i++)
    rc_stream_close (real->waiting[i]);
  if (real->pair_session >= 0)
    close (real->pair_session);
}

/* Return the next number of the generator with the state STATE
   (splitmix64).  */
static uint64_t
next_random (uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}

/* Put the end-session message of a session that does not exist in
   BYTES, which has room for 16 bytes, and return its size.  */
static size_t
end_bytes (char *bytes)
{
  struct rd_message end = { .type = RD_END_SESSION,
			    .u.end_session = { .reason = RD_END_NO_SESSION } };
  size_t size = rd_encoded_size (&end);
  if (size > 16)
    abort ();
  rd_encode (&end, (unsigned char *) bytes);
  return size;
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
  return listen_session (run);
}

/* Listen as the session of RUN at its socket.  Return false on an
   error.  */
static bool
listen_session (struct run *run)
{
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
