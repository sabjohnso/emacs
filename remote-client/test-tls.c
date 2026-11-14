/* The laws and examples of the TLS streams.


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

/* The server of each case is in the process of the test, on a socket
   that does not block, as in the relay.  The client is in a new
   process, on a socket that blocks, as in the window mode.  A pair of
   Unix-domain sockets connects them.

   - Pin: rc_tls_connect gives a stream if and only if the pin is the
     pin of the key of the server.  The wrong pins differ from the
     right pin in one random bit.
   - Echo: the bytes that the client writes come back unchanged from
     a server that writes back what it reads.  The client writes
     chunks of random sizes, also larger than a record of TLS.
   - Buffered: the client writes one record of a random size, and the
     server reads it in parts of random sizes with no poll after the
     first part.  A part can be RC_STREAM_RECORD_SIZE bytes.  After
     each read, the stream is buffered if and only if bytes of the
     record remain, and the bytes come unchanged.

   The examples:

   - Distinct: two new keys have different pins.
   - Plain: the server stream fails when the client sends bytes that
     are not TLS.
   - Version: the server refuses a client that offers only TLS 1.2.
   - Full: when the socket of the server cannot take the reply of the
     handshake, the server stream waits for POLLOUT, and the handshake
     completes when the client reads.
   - Signal: the handshake of rc_tls_connect goes on after a signal
     stops a wait.
   - Limit: rc_tls_connect stops after its limit when the server does
     not answer.
   - End: when the client closes its stream, the server stream ends
     with 0 and not with an error.

   The program runs TRIALS trials of each law from a seed that it gets
   from the clock.  With one argument, it runs the trial of that seed
   only.  */

#include <config.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <gnutls/gnutls.h>

#include "pin.h"
#include "stream.h"
#include "tls.h"

enum { TRIALS = 40 };

/* The longest wait for an event, in milliseconds.  */
enum { LIMIT = 5000 };

/* The largest number of bytes of the echo law.  */
enum { ECHO_SIZE = 100000 };

/* The largest number of bytes in a record of TLS.  */
enum { RECORD_SIZE = RC_STREAM_RECORD_SIZE };

/* What a client process does with its stream.  */
typedef bool (*client_action) (struct rc_stream *, uint64_t);

static const char *run_trial (uint64_t);
static bool pin_law (uint64_t *);
static bool echo_law (uint64_t *);
static bool buffered_law (uint64_t *);
static bool distinct_example (void);
static bool plain_example (void);
static bool version_example (void);
static bool full_example (void);
static bool signal_example (void);
static bool limit_example (void);
static ssize_t pull_after_filler (gnutls_transport_ptr_t, void *, size_t);
static void ignore_signal (int);
static bool end_example (void);
static bool close_client (struct rc_stream *, uint64_t);
static bool echo_client (struct rc_stream *, uint64_t);
static bool buffered_client (struct rc_stream *, uint64_t);
static struct rc_stream *start_pair (const struct rc_tls_key *,
				     const uint8_t *, client_action,
				     uint64_t, pid_t *);
static bool client_passed_p (pid_t);
static ptrdiff_t wait_read (struct rc_stream *, void *, size_t);
static bool write_all (struct rc_stream *, const unsigned char *, size_t);
static bool read_all (struct rc_stream *, unsigned char *, size_t);
static bool echo (struct rc_stream *);
static size_t record_size (uint64_t);
static void fill_bytes (uint64_t, unsigned char *, size_t);
static uint64_t random_below (uint64_t *, uint64_t);
static uint64_t random_next (uint64_t *);
static int64_t now_ms (void);

/* A law and its name.  */
static const struct
{
  const char *name;
  bool (*check) (uint64_t *);
} laws[] =
  {
    { "pin", pin_law },
    { "echo", echo_law },
    { "buffered", buffered_law },
  };

/* An example and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} examples[] =
  {
    { "distinct", distinct_example },
    { "plain", plain_example },
    { "version", version_example },
    { "full", full_example },
    { "signal", signal_example },
    { "limit", limit_example },
    { "end", end_example },
  };

int
main (int argc, char **argv)
{
  signal (SIGPIPE, SIG_IGN);
  for (size_t i = 0; i < sizeof examples / sizeof *examples; i++)
    if (!examples[i].check ())
      {
	fprintf (stderr, "test-tls: the %s example failed\n",
		 examples[i].name);
	return 1;
      }
  if (argc == 2)
    return run_trial (strtoull (argv[1], NULL, 10)) ? 1 : 0;
  uint64_t base = ((uint64_t) time (NULL) << 20 ^ getpid ()) * TRIALS;
  for (int trial = 0; trial < TRIALS; trial++)
    {
      const char *failed = run_trial (base + trial);
      if (failed)
	{
	  fprintf (stderr, "test-tls: the %s law failed with seed %llu\n",
		   failed, (unsigned long long) (base + trial));
	  return 1;
	}
    }
  printf ("test-tls: the examples and %d trials passed\n", TRIALS);
  return 0;
}

/* Run the trial of SEED: one case of each law.  Return the name of the
   first law that fails, or NULL if each law passes.  */
static const char *
run_trial (uint64_t seed)
{
  uint64_t state = seed;
  for (size_t i = 0; i < sizeof laws / sizeof *laws; i++)
    if (!laws[i].check (&state))
      return laws[i].name;
  return NULL;
}

/* The pin law, for a random pin of STATE: the right pin, or a pin
   with one changed bit.  */
static bool
pin_law (uint64_t *state)
{
  struct rc_tls_key *key = rc_tls_key_new ();
  if (!key)
    return false;
  uint8_t pin[RC_PIN_SIZE];
  rc_tls_key_pin (key, pin);
  bool right = random_below (state, 2) == 0;
  if (!right)
    {
      int bit = random_below (state, 8 * RC_PIN_SIZE);
      pin[bit / 8] ^= 1 << bit % 8;
    }
  pid_t client;
  struct rc_stream *server = start_pair (key, pin, close_client, 0, &client);
  unsigned char byte;
  ptrdiff_t result = server ? wait_read (server, &byte, 1) : 1;
  rc_stream_close (server);
  rc_tls_key_free (key);
  return (client_passed_p (client) == right
	  && (right ? result == 0 : result == RC_STREAM_FAIL));
}

/* The echo law, for a random size and random chunks of STATE.  */
static bool
echo_law (uint64_t *state)
{
  struct rc_tls_key *key = rc_tls_key_new ();
  if (!key)
    return false;
  uint8_t pin[RC_PIN_SIZE];
  rc_tls_key_pin (key, pin);
  pid_t client;
  struct rc_stream *server = start_pair (key, pin, echo_client,
					 random_next (state), &client);
  bool ok = server && echo (server);
  rc_stream_close (server);
  rc_tls_key_free (key);
  return client_passed_p (client) && ok;
}

/* The example of two new keys.  */
static bool
distinct_example (void)
{
  struct rc_tls_key *a = rc_tls_key_new (), *b = rc_tls_key_new ();
  uint8_t pin_a[RC_PIN_SIZE], pin_b[RC_PIN_SIZE];
  bool ok = a && b;
  if (ok)
    {
      rc_tls_key_pin (a, pin_a);
      rc_tls_key_pin (b, pin_b);
      ok = memcmp (pin_a, pin_b, RC_PIN_SIZE) != 0;
    }
  rc_tls_key_free (a);
  rc_tls_key_free (b);
  return ok;
}

/* The example of a client that does not speak TLS.  */
static bool
plain_example (void)
{
  struct rc_tls_key *key = rc_tls_key_new ();
  int fds[2];
  if (!key || socketpair (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds))
    return false;
  static const char text[] = "GET / HTTP/1.0\r\n\r\n";
  bool ok = write (fds[1], text, sizeof text) == sizeof text;
  struct rc_stream *server = rc_tls_accept (key, fds[0]);
  unsigned char byte;
  ok = ok && server && wait_read (server, &byte, 1) == RC_STREAM_FAIL;
  rc_stream_close (server);
  close (fds[1]);
  rc_tls_key_free (key);
  return ok;
}

/* The example of a client of TLS 1.2.  The client is GnuTLS with no
   stream, because a stream of tls.h offers only TLS 1.3.  */
static bool
version_example (void)
{
  struct rc_tls_key *key = rc_tls_key_new ();
  int fds[2];
  if (!key || socketpair (AF_UNIX, SOCK_STREAM, 0, fds))
    return false;
  pid_t client = fork ();
  if (client == 0)
    {
      close (fds[0]);
      gnutls_session_t session;
      gnutls_certificate_credentials_t credentials;
      gnutls_certificate_allocate_credentials (&credentials);
      gnutls_init (&session, GNUTLS_CLIENT);
      gnutls_priority_set_direct (session, "NORMAL:-VERS-ALL:+VERS-TLS1.2",
				  NULL);
      gnutls_credentials_set (session, GNUTLS_CRD_CERTIFICATE, credentials);
      gnutls_transport_set_int (session, fds[1]);
      gnutls_handshake_set_timeout (session, LIMIT);
      _exit (gnutls_handshake (session) < 0 ? 0 : 1);
    }
  close (fds[1]);
  fcntl (fds[0], F_SETFL, O_NONBLOCK);
  struct rc_stream *server = rc_tls_accept (key, fds[0]);
  unsigned char byte;
  bool ok = server && wait_read (server, &byte, 1) == RC_STREAM_FAIL;
  rc_stream_close (server);
  rc_tls_key_free (key);
  return client_passed_p (client) && ok;
}

/* The number of bytes that fill the socket of the server in the full
   example.  The client discards them before it reads the reply.  */
static size_t filler;

/* The example of a full socket of the server.  The client is GnuTLS
   with no stream, because its first read must discard the filler.  */
static bool
full_example (void)
{
  struct rc_tls_key *key = rc_tls_key_new ();
  int fds[2];
  if (!key || socketpair (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds))
    return false;
  static const unsigned char bytes[4096];
  filler = 0;
  for (ssize_t count; (count = write (fds[0], bytes, sizeof bytes)) > 0;)
    filler += count;
  uint8_t pin[RC_PIN_SIZE];
  rc_tls_key_pin (key, pin);
  pid_t client = fork ();
  if (client == 0)
    {
      close (fds[0]);
      fcntl (fds[1], F_SETFL, 0);
      gnutls_session_t session;
      gnutls_certificate_credentials_t credentials;
      gnutls_certificate_allocate_credentials (&credentials);
      gnutls_init (&session, GNUTLS_CLIENT);
      gnutls_priority_set_direct (session, "NORMAL:-VERS-ALL:+VERS-TLS1.3",
				  NULL);
      gnutls_credentials_set (session, GNUTLS_CRD_CERTIFICATE, credentials);
      gnutls_transport_set_int (session, fds[1]);
      /* With no time limit, GnuTLS calls only this pull function.
	 The server closes the socket after LIMIT, so the handshake
	 stops.  */
      gnutls_transport_set_pull_function (session, pull_after_filler);
      int result = gnutls_handshake (session);
      if (result >= 0)
	gnutls_bye (session, GNUTLS_SHUT_WR);
      _exit (result < 0 ? 1 : 0);
    }
  close (fds[1]);
  struct rc_stream *server = rc_tls_accept (key, fds[0]);
  unsigned char byte;
  bool ok = server && wait_read (server, &byte, 1) == 0;
  rc_stream_close (server);
  rc_tls_key_free (key);
  return client_passed_p (client) && ok;
}

/* The pull function of the client of the full example, on the socket
   PTR.  Before the first read, wait until the server waits, and then
   discard the filler.  */
static ssize_t
pull_after_filler (gnutls_transport_ptr_t ptr, void *bytes, size_t size)
{
  int fd = (intptr_t) ptr;
  if (filler > 0)
    {
      poll (NULL, 0, 300);
      unsigned char discard[4096];
      while (filler > 0)
	{
	  ssize_t count = read (fd, discard, (filler < sizeof discard
					      ? filler : sizeof discard));
	  if (count <= 0)
	    return -1;
	  filler -= count;
	}
    }
  return read (fd, bytes, size);
}

/* The example of signals in the handshake of the client.  The server
   waits before it answers, and a timer sends a signal each
   millisecond to the client.  */
static bool
signal_example (void)
{
  struct rc_tls_key *key = rc_tls_key_new ();
  if (!key)
    return false;
  uint8_t pin[RC_PIN_SIZE];
  rc_tls_key_pin (key, pin);
  int fds[2];
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds))
    return false;
  pid_t client = fork ();
  if (client == 0)
    {
      close (fds[0]);
      struct sigaction action = { .sa_handler = ignore_signal };
      sigaction (SIGALRM, &action, NULL);
      struct itimerval timer = { { 0, 1000 }, { 0, 1000 } };
      setitimer (ITIMER_REAL, &timer, NULL);
      struct rc_stream *stream = rc_tls_connect (fds[1], pin, LIMIT);
      setitimer (ITIMER_REAL, &(struct itimerval) { 0 }, NULL);
      rc_stream_close (stream);
      _exit (stream ? 0 : 1);
    }
  close (fds[1]);
  fcntl (fds[0], F_SETFL, O_NONBLOCK);
  poll (NULL, 0, 200);
  struct rc_stream *server = rc_tls_accept (key, fds[0]);
  unsigned char byte;
  bool ok = server && wait_read (server, &byte, 1) == 0;
  rc_stream_close (server);
  rc_tls_key_free (key);
  return client_passed_p (client) && ok;
}

/* A handler that does nothing, so a signal only stops a wait.  */
static void
ignore_signal (int signal)
{
  (void) signal;
}

/* The example of a server that does not answer.  */
static bool
limit_example (void)
{
  enum { WAIT = 300 };
  int fds[2];
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds))
    return false;
  uint8_t pin[RC_PIN_SIZE] = { 0 };
  int64_t start = now_ms ();
  struct rc_stream *stream = rc_tls_connect (fds[0], pin, WAIT);
  int64_t time = now_ms () - start;
  close (fds[1]);
  rc_stream_close (stream);
  return !stream && time >= WAIT && time < LIMIT;
}

/* The buffered law, for a random record and random reads of
   STATE.  */
static bool
buffered_law (uint64_t *state)
{
  struct rc_tls_key *key = rc_tls_key_new ();
  if (!key)
    return false;
  uint8_t pin[RC_PIN_SIZE];
  rc_tls_key_pin (key, pin);
  uint64_t seed = random_next (state);
  size_t size = record_size (seed);
  /* A part can go past the end of the record.  */
  static unsigned char sent[RECORD_SIZE], got[2 * RECORD_SIZE];
  fill_bytes (seed, sent, size);
  pid_t client;
  struct rc_stream *server = start_pair (key, pin, buffered_client, seed,
					 &client);
  size_t done = 0;
  bool ok = server != NULL;
  while (ok && done < size)
    {
      size_t left = size - done;
      size_t part = (random_below (state, 4) == 0 ? RC_STREAM_RECORD_SIZE
		     : 1 + random_below (state, left));
      ptrdiff_t count = (done == 0 ? wait_read (server, got, part)
			 : rc_stream_read (server, got + done, part));
      ok = count > 0 && (size_t) count == (part < left ? part : left);
      if (ok)
	done += count;
      ok = ok && rc_stream_buffered_p (server) == (done < size);
    }
  ok = ok && memcmp (sent, got, size) == 0;
  rc_stream_close (server);
  rc_tls_key_free (key);
  return client_passed_p (client) && ok;
}

/* The example of the end of the client.  */
static bool
end_example (void)
{
  struct rc_tls_key *key = rc_tls_key_new ();
  if (!key)
    return false;
  uint8_t pin[RC_PIN_SIZE];
  rc_tls_key_pin (key, pin);
  pid_t client;
  struct rc_stream *server = start_pair (key, pin, close_client, 0, &client);
  unsigned char byte;
  bool ok = server && wait_read (server, &byte, 1) == 0;
  rc_stream_close (server);
  rc_tls_key_free (key);
  return client_passed_p (client) && ok;
}

/* The action of a client that only closes its stream.  */
static bool
close_client (struct rc_stream *stream, uint64_t seed)
{
  (void) seed;
  rc_stream_close (stream);
  return true;
}

/* The action of the client of the echo law: write chunks of random
   sizes of SEED, and read each one back.  */
static bool
echo_client (struct rc_stream *stream, uint64_t seed)
{
  static unsigned char sent[ECHO_SIZE], got[ECHO_SIZE];
  uint64_t state = seed;
  size_t size = 1 + random_below (&state, ECHO_SIZE);
  fill_bytes (seed, sent, size);
  bool ok = true;
  for (size_t done = 0, chunk; ok && done < size; done += chunk)
    {
      chunk = 1 + random_below (&state, size - done);
      ok = (write_all (stream, sent + done, chunk)
	    && read_all (stream, got + done, chunk)
	    && memcmp (sent + done, got + done, chunk) == 0);
    }
  rc_stream_close (stream);
  return ok;
}

/* The action of the client of the buffered law: write the record of
   SEED in one write.  */
static bool
buffered_client (struct rc_stream *stream, uint64_t seed)
{
  static unsigned char bytes[RECORD_SIZE];
  size_t size = record_size (seed);
  fill_bytes (seed, bytes, size);
  bool ok = rc_stream_write (stream, bytes, size) == (ptrdiff_t) size;
  unsigned char byte;
  ok = ok && rc_stream_read (stream, &byte, 1) == 0;
  rc_stream_close (stream);
  return ok;
}

/* Connect a new client process to a new server stream with KEY.  The
   client connects with PIN, and then does ACTION with SEED.  Put the
   client in *CLIENT, and return the server stream, or NULL.  */
static struct rc_stream *
start_pair (const struct rc_tls_key *key, const uint8_t *pin,
	    client_action action, uint64_t seed, pid_t *client)
{
  int fds[2];
  *client = -1;
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds))
    return NULL;
  *client = fork ();
  if (*client == 0)
    {
      close (fds[0]);
      struct rc_stream *stream = rc_tls_connect (fds[1], pin, LIMIT);
      _exit (stream && action (stream, seed) ? 0 : 1);
    }
  close (fds[1]);
  fcntl (fds[0], F_SETFL, O_NONBLOCK);
  return rc_tls_accept (key, fds[0]);
}

/* Wait for the end of the process CLIENT, and return true if it exits
   with the status 0.  */
static bool
client_passed_p (pid_t client)
{
  int status;
  return (client > 0 && waitpid (client, &status, 0) == client
	  && WIFEXITED (status) && WEXITSTATUS (status) == 0);
}

/* Read at most SIZE bytes of STREAM into BYTES, and poll while the
   stream waits, but at most LIMIT milliseconds.  Return the result of
   the read, or RC_STREAM_WAIT after LIMIT.  */
static ptrdiff_t
wait_read (struct rc_stream *stream, void *bytes, size_t size)
{
  int64_t end = now_ms () + LIMIT;
  for (;;)
    {
      ptrdiff_t result = rc_stream_read (stream, bytes, size);
      int64_t left = end - now_ms ();
      if (result != RC_STREAM_WAIT || left <= 0)
	return result;
      struct pollfd ready = { rc_stream_fd (stream),
			      rc_stream_events (stream, POLLIN), 0 };
      poll (&ready, 1, left);
    }
}

/* Write the SIZE BYTES to STREAM, which blocks.  Return false on an
   error.  */
static bool
write_all (struct rc_stream *stream, const unsigned char *bytes,
	   size_t size)
{
  while (size > 0)
    {
      ptrdiff_t count = rc_stream_write (stream, bytes, size);
      if (count == RC_STREAM_FAIL)
	return false;
      if (count > 0)
	{
	  bytes += count;
	  size -= count;
	}
    }
  return true;
}

/* Read SIZE bytes of STREAM, which blocks, into BYTES.  Return false at
   the end or on an error.  */
static bool
read_all (struct rc_stream *stream, unsigned char *bytes, size_t size)
{
  while (size > 0)
    {
      ptrdiff_t count = rc_stream_read (stream, bytes, size);
      if (count == 0 || count == RC_STREAM_FAIL)
	return false;
      if (count > 0)
	{
	  bytes += count;
	  size -= count;
	}
    }
  return true;
}

/* Write back each byte of the server stream STREAM until its end.
   Return true if the stream ends with 0.  */
static bool
echo (struct rc_stream *stream)
{
  unsigned char bytes[1 << 16];
  for (;;)
    {
      ptrdiff_t count = wait_read (stream, bytes, sizeof bytes);
      if (count <= 0)
	return count == 0;
      for (ptrdiff_t done = 0; done < count;)
	{
	  ptrdiff_t written = rc_stream_write (stream, bytes + done,
					       count - done);
	  if (written == RC_STREAM_FAIL)
	    return false;
	  if (written > 0)
	    done += written;
	  else
	    {
	      struct pollfd ready = { rc_stream_fd (stream),
				      rc_stream_events (stream, POLLOUT), 0 };
	      if (poll (&ready, 1, LIMIT) != 1)
		return false;
	    }
	}
    }
}

/* Return the size of the record of the buffered law for SEED.  */
static size_t
record_size (uint64_t seed)
{
  uint64_t state = seed;
  return 1 + random_below (&state, RECORD_SIZE);
}

/* Put SIZE bytes of the seed SEED in BYTES.  */
static void
fill_bytes (uint64_t seed, unsigned char *bytes, size_t size)
{
  uint64_t state = seed;
  for (size_t i = 0; i < size; i++)
    bytes[i] = random_next (&state);
}

/* Return a random number from 0 to LIMIT - 1.  */
static uint64_t
random_below (uint64_t *state, uint64_t limit)
{
  return random_next (state) % limit;
}

/* Return the next number of the generator splitmix64.  */
static uint64_t
random_next (uint64_t *state)
{
  uint64_t z = (*state += 0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
  z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
  return z ^ (z >> 31);
}

/* Return the time of a monotonic clock in milliseconds.  */
static int64_t
now_ms (void)
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
