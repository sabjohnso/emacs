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

/* The test starts "remote-client --relay SOCKET" with a socket of the
   test.  The test acts as the session at SOCKET, and as the client of
   the relay.  The relay opens no window, so the test needs no
   display.

   - Line: the relay writes one valid line, and then closes its
     standard input, standard output, and standard error output, so
     SSH can stop.  The process that the test started exits with the
     status 0.  The relay is in a new session, so a hangup signal to
     the group of that process does not stop the relay.
   - Relay: after a client sends the token, the relay connects to the
     session, and copies the bytes in the two directions.  The test
     sends more bytes than the buffers of the relay and of the kernel
     can hold, so both directions must flow at the same time.
   - Wrong: the relay closes a connection with a wrong token, and does
     not connect to the session.  A next client with the token works.
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
   - End: when the relay cannot connect to the session, it closes the
     client and exits.
   - Loopback: the relay refuses connections to an address that is
     not the loopback address.
   - Missing: when SOCKET is not a socket, the relay writes no line and
     exits with the status 1.
   - Usage: with no SOCKET, the program exits with the status 1.  */

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

#include "pending.h"
#include "relay.h"
#include "relayline.h"

/* The longest wait for an event of the relay, in milliseconds.  */
enum { LIMIT = 5000 };

/* The shortest wait that shows that an event does not occur.  */
enum { QUIET = 300 };

/* The number of bytes that the relay example sends.  */
enum { BULK = 16 << 20 };

/* One run of the relay: the directory and the socket of the session,
   the listening socket of the session, and the line of the relay.  */
struct run
{
  char directory[64];
  char socket[100];
  int session;
  struct rc_relay_line line;
};

static bool line_example (void);
static bool relay_example (void);
static bool wrong_example (void);
static bool idle_example (void);
static bool late_example (void);
static bool split_example (void);
static bool abandon_example (void);
static bool overflow_example (void);
static bool replace_example (void);
static bool session_end_example (void);
static bool end_example (void);
static bool loopback_example (void);
static bool missing_example (void);
static bool usage_example (void);
static bool start_run (struct run *);
static bool stop_run (struct run *);
static int start_relay (const char *, int *, int *, int *);
static int connect_idle (const struct run *);
static bool works_p (const struct run *);
static bool write_closed_p (int);
static bool read_line (int, char *, size_t);
static int connect_client (const struct run *, bool);
static int connect_to_port (struct in_addr, uint16_t);
static int accept_session (const struct run *, int);
static bool closed_p (int, int);
static bool echo_through (int, int, size_t);
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
    { "idle", idle_example },
    { "late", late_example },
    { "split", split_example },
    { "abandon", abandon_example },
    { "overflow", overflow_example },
    { "replace", replace_example },
    { "session end", session_end_example },
    { "end", end_example },
    { "loopback", loopback_example },
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
  int client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  bool ok = session >= 0 && echo_through (client, session, BULK);
  close (client);
  close (session);
  return stop_run (&run) && ok;
}

static bool
wrong_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  int wrong = connect_client (&run, false);
  bool ok = closed_p (wrong, LIMIT) && accept_session (&run, QUIET) < 0;
  int client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  ok = ok && session >= 0 && echo_through (client, session, 1000);
  close (wrong);
  close (client);
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
  int client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  bool ok = idle >= 0 && session >= 0 && echo_through (client, session, 1000);
  close (idle);
  close (client);
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
  int client = connect_idle (&run);
  bool ok = (client >= 0
	     && write (client, run.line.token, 10) == 10
	     && poll (NULL, 0, QUIET) == 0
	     && write (client, run.line.token + 10, RC_TOKEN_SIZE - 10)
		== RC_TOKEN_SIZE - 10);
  int session = accept_session (&run, LIMIT);
  ok = ok && session >= 0 && echo_through (client, session, 1000);
  close (client);
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
  int part = connect_idle (&run);
  bool ok = part >= 0 && write (part, run.line.token, 10) == 10;
  close (part);
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
  int old_client = connect_client (&run, true);
  int old_session = accept_session (&run, LIMIT);
  int client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  bool ok = (old_session >= 0 && session >= 0
	     && closed_p (old_client, LIMIT) && closed_p (old_session, LIMIT)
	     && echo_through (client, session, 1000));
  close (old_client);
  close (old_session);
  close (client);
  close (session);
  return stop_run (&run) && ok;
}

static bool
session_end_example (void)
{
  struct run run;
  if (!start_run (&run))
    return false;
  int client = connect_client (&run, true);
  int session = accept_session (&run, LIMIT);
  close (session);
  bool ok = session >= 0 && closed_p (client, LIMIT);
  close (client);
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
  int client = connect_client (&run, true);
  bool ok = closed_p (client, LIMIT);
  close (client);
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
missing_example (void)
{
  int in, out, err;
  pid_t pid = start_relay ("/nonexistent/socket", &in, &out, &err);
  char text[RC_RELAY_LINE_SIZE];
  bool ok = !read_line (out, text, sizeof text);
  close (in);
  close (out);
  close (err);
  return wait_for_exit (pid) == 1 && ok;
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
  return wait_for_exit (pid) == 1;
}

/* Make a session socket for RUN, start the relay, and read its line.
   Check that the relay closes its standard streams, that the process
   that the test started exits with the status 0, and that the relay
   lives after a hangup signal to the group of that process.  */
static bool
start_run (struct run *run)
{
  strcpy (run->directory, "/tmp/test-relay-XXXXXX");
  if (!mkdtemp (run->directory))
    return false;
  snprintf (run->socket, sizeof run->socket, "%s/socket", run->directory);
  run->session = socket (AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  snprintf (address.sun_path, sizeof address.sun_path, "%s", run->socket);
  if (run->session < 0
      || bind (run->session, (struct sockaddr *) &address,
	       sizeof address) != 0
      || listen (run->session, 4) != 0)
    return false;
  int in, out, err;
  pid_t pid = start_relay (run->socket, &in, &out, &err);
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
  int client = connect_client (run, true);
  bool ok = client < 0 || closed_p (client, LIMIT);
  if (client >= 0)
    close (client);
  for (int64_t start = now_ms (); now_ms () - start < LIMIT;
       poll (NULL, 0, 10))
    {
      int fd = connect_to_port ((struct in_addr) { htonl (INADDR_LOOPBACK) },
				run->line.port);
      if (fd < 0)
	return ok;
      close (fd);
    }
  return false;
}

/* Start "remote-client --relay SOCKET" as the leader of a new group of
   processes.  Put the write end of its standard input in *IN, and the
   read ends of its standard output and standard error output in *OUT
   and *ERR.  Return its process.  */
static int
start_relay (const char *socket, int *in, int *out, int *err)
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
   connection to the session, and the bytes go through.  */
static bool
works_p (const struct run *run)
{
  int client = connect_client (run, true);
  int session = accept_session (run, LIMIT);
  bool ok = client >= 0 && session >= 0 && echo_through (client, session, 100);
  close (client);
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

/* Connect a client to the relay of RUN, and send the token of the
   relay if GOOD, or else a wrong token.  Return the connection.  */
static int
connect_client (const struct run *run, bool good)
{
  int fd = connect_to_port ((struct in_addr) { htonl (INADDR_LOOPBACK) },
			    run->line.port);
  uint8_t token[RC_TOKEN_SIZE];
  memcpy (token, run->line.token, sizeof token);
  if (!good)
    token[RC_TOKEN_SIZE - 1] ^= 1;
  if (fd >= 0 && write (fd, token, sizeof token) != sizeof token)
    {
      close (fd);
      return -1;
    }
  return fd;
}

/* Connect a client to the relay of RUN, and send nothing.  Return the
   connection.  */
static int
connect_idle (const struct run *run)
{
  return connect_to_port ((struct in_addr) { htonl (INADDR_LOOPBACK) },
			  run->line.port);
}

/* Return a connection to PORT at ADDRESS, or -1.  */
static int
connect_to_port (struct in_addr address, uint16_t port)
{
  struct sockaddr_in socket_address = { .sin_family = AF_INET,
					.sin_port = htons (port),
					.sin_addr = address };
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd >= 0
      && connect (fd, (struct sockaddr *) &socket_address,
		  sizeof socket_address) != 0)
    {
      close (fd);
      return -1;
    }
  return fd;
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

/* Send SIZE bytes on CLIENT.  Act as a session that sends back each
   byte that comes on SESSION.  Return true if CLIENT gets the same
   bytes again.  All the sockets are nonblocking, so the bytes flow in
   the two directions at the same time.

   First the client reads nothing, until no write is possible for
   QUIET milliseconds.  Then each buffer along the path is full, so
   the relay must also write parts of its buffers.  */
static bool
echo_through (int client, int session, size_t size)
{
  static unsigned char echo[1 << 16];
  size_t sent = 0, got = 0, echo_start = 0, echo_end = 0;
  bool stalled = true;
  fcntl (client, F_SETFL, O_NONBLOCK);
  fcntl (session, F_SETFL, O_NONBLOCK);
  int64_t start = now_ms ();
  while (got < size)
    {
      if (now_ms () - start > 4 * LIMIT)
	return false;
      struct pollfd fds[2]
	= { { .fd = client,
	      .events = (stalled ? 0 : POLLIN) | (sent < size ? POLLOUT : 0) },
	    { .fd = session,
	      .events = echo_start < echo_end ? POLLOUT : POLLIN } };
      int ready = poll (fds, 2, stalled ? QUIET : LIMIT);
      if (ready < 0 || (ready == 0 && !stalled))
	return false;
      if (ready == 0 || sent == size)
	stalled = false;
      if (fds[0].revents & POLLOUT)
	{
	  unsigned char bytes[4096];
	  size_t count = size - sent < sizeof bytes ? size - sent
						    : sizeof bytes;
	  for (size_t i = 0; i < count; i++)
	    bytes[i] = (sent + i) * 7;
	  ssize_t written = write (client, bytes, count);
	  if (written > 0)
	    sent += written;
	}
      if (!stalled && fds[0].revents & (POLLIN | POLLHUP | POLLERR))
	{
	  unsigned char bytes[4096];
	  ssize_t count = read (client, bytes, sizeof bytes);
	  if (count <= 0)
	    return false;
	  for (ssize_t i = 0; i < count; i++)
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
