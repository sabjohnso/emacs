/* Test the bootstrap of remote-client through SSH.

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

/* The test starts "remote-client --ssh-command=FAKE --ssh HOST".  FAKE
   is this program under the name fake-ssh.  The test acts as the
   session, and the real relay of remote-client connects the session
   and the client.  The program shows windows, so the check of the
   Makefile runs this test in Xvfb.

   As the bootstrap, the fake runs the remote command with sh.  A fake
   emacsclient gives the session socket of the test.  The fake keeps a
   copy of the line of the relay, so the test can stop the relay at the
   end.  As the tunnel, the fake forwards its local socket to the port
   of the relay, and writes its process to a file.

   - Start: the client runs the bootstrap and the tunnel, and sends the
     token, so the session gets a connection.  When the session removes
     its frame and closes, the client exits with the status 0.  Then
     no tunnel runs, and the directory of the local socket is gone.
   - Tunnel: when the tunnel stops, the client starts it again, and the
     session gets a new connection.
   - Fail: when the bootstrap gives no line, the client exits with the
     status 1.
   - No tunnel: when the tunnel stops before the first connection, and
     stops again, the client exits with the status 1 at once.
   - Descriptors: SSH gets no open descriptor of the client other than
     its standard streams, also when the client starts it again.
   - Usage: --ssh with no host, with a socket, or with --render-to makes
     the program exit with the status 1.  */

#include <config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "rdproto.h"
#include "relayline.h"

/* The longest wait for an event, in milliseconds.  The first connection
   also waits for the start of GTK.  */
enum { LIMIT = 10000 };

/* The timeout of the program in the examples, in milliseconds.  */
enum { TIMEOUT = 600 };

/* The frame of each example.  */
enum { FRAME = 7 };

/* One run of the program.  */
struct run
{
  char directory[64];
  char session[128];
  pid_t pid;
  int listener;
  int connection;
};

static bool start_example (void);
static bool tunnel_example (void);
static bool fail_example (void);
static bool no_tunnel_example (void);
static bool usage_example (void);
static bool start_run (struct run *, const char *);
static void stop_run (struct run *);
static bool end_session (struct run *);
static bool clean_after_exit (const struct run *);
static bool no_leaks_p (const struct run *);
static int open_descriptors (void);
static int read_tunnels (const struct run *, pid_t *, int);
static void stop_relay (const struct run *);
static bool make_bin (const struct run *);
static int accept_session (const struct run *);
static bool send_message (int, const struct rd_message *);
static int wait_for_exit (pid_t);
static int run_usage (const char *const *);
static int fake_ssh (int, char **);
static int fake_bootstrap (const char *);
static int fake_tunnel (const char *);
static void splice_fds (int, int);
static int64_t now_ms (void);

/* An example and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} examples[] =
  {
    { "start", start_example },
    { "tunnel", tunnel_example },
    { "fail", fail_example },
    { "no tunnel", no_tunnel_example },
    { "usage", usage_example },
  };

int
main (int argc, char **argv)
{
  if (strcmp (basename (argv[0]), "fake-ssh") == 0)
    return fake_ssh (argc, argv);
  signal (SIGPIPE, SIG_IGN);
  for (size_t i = 0; i < sizeof examples / sizeof *examples; i++)
    if (!examples[i].check ())
      {
	fprintf (stderr, "test-ssh: the %s example failed\n",
		 examples[i].name);
	return 1;
      }
  printf ("test-ssh: %zu examples passed\n",
	  sizeof examples / sizeof *examples);
  return 0;
}

static bool
start_example (void)
{
  struct run run;
  if (!start_run (&run, NULL))
    return false;
  bool ok = end_session (&run) && clean_after_exit (&run)
    && no_leaks_p (&run);
  stop_run (&run);
  return ok;
}

static bool
tunnel_example (void)
{
  struct run run;
  if (!start_run (&run, NULL))
    return false;
  pid_t tunnels[4];
  bool ok = read_tunnels (&run, tunnels, 4) == 1;
  if (ok)
    kill (tunnels[0], SIGKILL);
  close (run.connection);
  run.connection = accept_session (&run);
  ok = (ok && run.connection >= 0
	&& read_tunnels (&run, tunnels, 4) == 2
	&& end_session (&run) && clean_after_exit (&run)
	&& no_leaks_p (&run));
  stop_run (&run);
  return ok;
}

static bool
fail_example (void)
{
  struct run run;
  bool started = start_run (&run, "RD_TEST_FAIL");
  bool ok = !started && wait_for_exit (run.pid) == 1;
  run.pid = 0;
  ok = ok && clean_after_exit (&run);
  stop_run (&run);
  return ok;
}

static bool
no_tunnel_example (void)
{
  struct run run;
  bool started = start_run (&run, "RD_TEST_TUNNEL_FAIL");
  bool ok = !started && wait_for_exit (run.pid) == 1;
  run.pid = 0;
  ok = ok && clean_after_exit (&run);
  stop_run (&run);
  return ok;
}

static bool
usage_example (void)
{
  static const char *const no_host[] = { "--ssh", NULL };
  static const char *const socket[] = { "--ssh", "host", "socket", NULL };
  static const char *const png[] = { "--render-to=x.png", "--ssh", "host",
				     NULL };
  return (run_usage (no_host) == 1 && run_usage (socket) == 1
	  && run_usage (png) == 1);
}

/* Make the directory of RUN, listen as the session, and start the
   program.  If FAIL is not NULL, it is the variable of the environment
   that makes a part of the fake fail.  Return true if the session gets
   a connection; then define a frame on it.  */
static bool
start_run (struct run *run, const char *fail)
{
  *run = (struct run) { .listener = -1, .connection = -1 };
  strcpy (run->directory, "/tmp/test-ssh-XXXXXX");
  if (!mkdtemp (run->directory) || !make_bin (run))
    return false;
  snprintf (run->session, sizeof run->session, "%s/session",
	    run->directory);
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  snprintf (address.sun_path, sizeof address.sun_path, "%s", run->session);
  run->listener = socket (AF_UNIX, SOCK_STREAM, 0);
  if (run->listener < 0
      || bind (run->listener, (struct sockaddr *) &address,
	       sizeof address) != 0
      || listen (run->listener, 4) != 0)
    return false;
  char bin[128], path[4096], ssh[160], runtime[96], timeout[32];
  snprintf (bin, sizeof bin, "%s/bin", run->directory);
  snprintf (path, sizeof path, "%s:%s", bin, getenv ("PATH"));
  snprintf (ssh, sizeof ssh, "--ssh-command=%s/fake-ssh", bin);
  snprintf (runtime, sizeof runtime, "%s/run", run->directory);
  snprintf (timeout, sizeof timeout, "--timeout=%d", TIMEOUT);
  mkdir (runtime, 0700);
  run->pid = fork ();
  if (run->pid == 0)
    {
      setenv ("PATH", path, 1);
      setenv ("RD_TEST_DIR", run->directory, 1);
      setenv ("RD_TEST_SESSION", run->session, 1);
      setenv ("XDG_RUNTIME_DIR", runtime, 1);
      setenv ("XDG_CACHE_HOME", run->directory, 1);
      if (fail)
	setenv (fail, "1", 1);
      execl ("./remote-client", "remote-client", timeout, ssh, "--ssh",
	     "testhost", (char *) NULL);
      _exit (127);
    }
  if (fail)
    return false;
  run->connection = accept_session (run);
  struct rd_message define
    = { .type = RD_DEFINE_FRAME,
	.u.define_frame = { .frame = FRAME, .width = 200, .height = 100 } };
  return run->connection >= 0 && send_message (run->connection, &define);
}

/* Remove the frame of RUN, close the session, and return true if the
   program exits with the status 0.  */
static bool
end_session (struct run *run)
{
  struct rd_message remove = { .type = RD_DELETE_FRAME,
			       .u.delete_frame = { .frame = FRAME } };
  bool ok = send_message (run->connection, &remove);
  close (run->connection);
  run->connection = -1;
  ok = ok && wait_for_exit (run->pid) == 0;
  run->pid = 0;
  return ok;
}

/* Return true if no tunnel of RUN runs, and the directory of the
   local sockets of RUN is empty.  */
static bool
clean_after_exit (const struct run *run)
{
  pid_t tunnels[8];
  int count = read_tunnels (run, tunnels, 8);
  for (int64_t start = now_ms (); now_ms () - start < LIMIT;
       poll (NULL, 0, 10))
    {
      bool running = false;
      for (int i = 0; i < count; i++)
	running |= kill (tunnels[i], 0) == 0;
      if (!running)
	break;
    }
  for (int i = 0; i < count; i++)
    if (kill (tunnels[i], 0) == 0)
      return false;
  char runtime[96];
  snprintf (runtime, sizeof runtime, "%s/run", run->directory);
  return rmdir (runtime) == 0;
}

/* Return true if each run of the fake SSH of RUN got no descriptor
   other than its standard streams.  The fake writes the number of the
   other descriptors to the file descriptors.  */
static bool
no_leaks_p (const struct run *run)
{
  char name[128];
  snprintf (name, sizeof name, "%s/descriptors", run->directory);
  FILE *file = fopen (name, "r");
  int count, runs = 0;
  bool ok = file != NULL;
  while (ok && fscanf (file, "%d", &count) == 1)
    {
      runs++;
      ok = count == 0;
    }
  if (file)
    fclose (file);
  return ok && runs >= 2;
}

/* Put the processes of the tunnels of RUN in TUNNELS, at most LIMIT of
   them, and return their number.  */
static int
read_tunnels (const struct run *run, pid_t *tunnels, int limit)
{
  char name[128];
  snprintf (name, sizeof name, "%s/tunnels", run->directory);
  FILE *file = fopen (name, "r");
  int count = 0;
  long pid;
  while (file && count < limit && fscanf (file, "%ld", &pid) == 1)
    tunnels[count++] = pid;
  if (file)
    fclose (file);
  return count;
}

/* Stop the program and the relay of RUN, and remove its directory.  */
static void
stop_run (struct run *run)
{
  if (run->pid > 0)
    {
      kill (run->pid, SIGKILL);
      waitpid (run->pid, NULL, 0);
    }
  if (run->connection >= 0)
    close (run->connection);
  if (run->listener >= 0)
    close (run->listener);
  unlink (run->session);
  stop_relay (run);
  pid_t tunnels[8];
  for (int i = read_tunnels (run, tunnels, 8); i > 0; i--)
    kill (tunnels[i - 1], SIGKILL);
  char command[128];
  snprintf (command, sizeof command, "rm -rf '%s'", run->directory);
  if (system (command) != 0)
    fprintf (stderr, "test-ssh: cannot remove %s\n", run->directory);
}

/* Stop the relay of RUN: its session socket is gone, so a client with
   the token makes it exit.  */
static void
stop_relay (const struct run *run)
{
  char name[128], text[RC_RELAY_LINE_SIZE] = "";
  snprintf (name, sizeof name, "%s/relay.line", run->directory);
  FILE *file = fopen (name, "r");
  struct rc_relay_line line;
  bool found = (file && fgets (text, sizeof text, file)
		&& rc_relay_line_parse (text, &line));
  if (file)
    fclose (file);
  if (!found)
    return;
  struct sockaddr_in address = { .sin_family = AF_INET,
				 .sin_port = htons (line.port),
				 .sin_addr = { htonl (INADDR_LOOPBACK) } };
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd >= 0
      && connect (fd, (struct sockaddr *) &address, sizeof address) == 0
      && write (fd, line.token, RC_TOKEN_SIZE) == RC_TOKEN_SIZE)
    {
      struct pollfd ready = { .fd = fd, .events = POLLIN };
      poll (&ready, 1, LIMIT);
    }
  if (fd >= 0)
    close (fd);
}

/* Make the directory bin of RUN: fake-ssh is this program,
   remote-client is the program of the test, and emacsclient prints the
   session socket as Emacs prints a string.  */
static bool
make_bin (const struct run *run)
{
  char bin[128], name[192], target[4096];
  snprintf (bin, sizeof bin, "%s/bin", run->directory);
  if (mkdir (bin, 0700) != 0 || !getcwd (target, sizeof target - 32))
    return false;
  size_t length = strlen (target);
  snprintf (name, sizeof name, "%s/fake-ssh", bin);
  strcpy (target + length, "/test-ssh");
  if (symlink (target, name) != 0)
    return false;
  snprintf (name, sizeof name, "%s/remote-client", bin);
  strcpy (target + length, "/remote-client");
  if (symlink (target, name) != 0)
    return false;
  snprintf (name, sizeof name, "%s/emacsclient", bin);
  FILE *file = fopen (name, "w");
  if (!file)
    return false;
  fputs ("#!/bin/sh\nprintf '\"%s\"\\n' \"$RD_TEST_SESSION\"\n", file);
  fclose (file);
  return chmod (name, 0700) == 0;
}

/* Return the next connection of the session of RUN, or -1 if none
   comes in LIMIT milliseconds.  */
static int
accept_session (const struct run *run)
{
  struct pollfd ready = { .fd = run->listener, .events = POLLIN };
  if (poll (&ready, 1, LIMIT) != 1)
    return -1;
  return accept (run->listener, NULL, NULL);
}

/* Send MESSAGE on FD.  Return true on success.  */
static bool
send_message (int fd, const struct rd_message *message)
{
  unsigned char bytes[64];
  size_t size = rd_encoded_size (message);
  if (size > sizeof bytes)
    return false;
  rd_encode (message, bytes);
  return send (fd, bytes, size, MSG_NOSIGNAL) == (ssize_t) size;
}

/* Wait for the exit of the process PID, and return its status, or -1
   if it does not exit normally in LIMIT milliseconds.  */
static int
wait_for_exit (pid_t pid)
{
  for (int64_t start = now_ms (); now_ms () - start < LIMIT;
       poll (NULL, 0, 10))
    {
      int status;
      if (waitpid (pid, &status, WNOHANG) == pid)
	return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
    }
  kill (pid, SIGKILL);
  waitpid (pid, NULL, 0);
  return -1;
}

/* Run the program with the arguments ARGUMENTS, and return its exit
   status.  */
static int
run_usage (const char *const *arguments)
{
  pid_t pid = fork ();
  if (pid == 0)
    {
      const char *argv[8] = { "remote-client" };
      for (int i = 0; arguments[i] && i < 6; i++)
	argv[i + 1] = arguments[i];
      int null = open ("/dev/null", O_WRONLY);
      dup2 (null, STDERR_FILENO);
      execv ("./remote-client", (char **) argv);
      _exit (127);
    }
  return wait_for_exit (pid);
}

/* The fake SSH.  With -N, it is the tunnel of the forward after -L.
   Otherwise, it runs the last argument with sh.  */
static int
fake_ssh (int argc, char **argv)
{
  char name[128];
  snprintf (name, sizeof name, "%s/descriptors", getenv ("RD_TEST_DIR"));
  int count = open_descriptors ();
  FILE *file = fopen (name, "a");
  if (file)
    {
      fprintf (file, "%d\n", count);
      fclose (file);
    }
  const char *forward = NULL;
  bool tunnel = false;
  for (int i = 1; i < argc; i++)
    if (strcmp (argv[i], "-N") == 0)
      tunnel = true;
    else if (strcmp (argv[i], "-L") == 0 && i + 1 < argc)
      forward = argv[i + 1];
  if (tunnel)
    return (forward && strcmp (argv[argc - 2], "--") == 0
	    && !getenv ("RD_TEST_TUNNEL_FAIL")
	    ? fake_tunnel (forward) : 255);
  if (argc < 4 || strcmp (argv[argc - 3], "--") != 0)
    return 255;
  if (getenv ("RD_TEST_FAIL"))
    return 255;
  return fake_bootstrap (argv[argc - 1]);
}

/* Return the number of the open descriptors of this process other
   than the standard streams.  */
static int
open_descriptors (void)
{
  int count = 0;
  for (int fd = 3; fd < 1024; fd++)
    count += fcntl (fd, F_GETFD) != -1;
  return count;
}

/* Run COMMAND with sh.  Copy its output to the standard output and to
   the file relay.line.  */
static int
fake_bootstrap (const char *command)
{
  int fds[2];
  if (pipe (fds) != 0)
    return 255;
  pid_t pid = fork ();
  if (pid == 0)
    {
      dup2 (fds[1], STDOUT_FILENO);
      close (fds[0]);
      close (fds[1]);
      execl ("/bin/sh", "sh", "-c", command, (char *) NULL);
      _exit (127);
    }
  close (fds[1]);
  char name[128];
  snprintf (name, sizeof name, "%s/relay.line", getenv ("RD_TEST_DIR"));
  FILE *copy = fopen (name, "w");
  char byte;
  while (read (fds[0], &byte, 1) == 1)
    {
      if (write (STDOUT_FILENO, &byte, 1) != 1)
	break;
      if (copy)
	fputc (byte, copy);
    }
  if (copy)
    fclose (copy);
  int status;
  waitpid (pid, &status, 0);
  return WIFEXITED (status) ? WEXITSTATUS (status) : 255;
}

/* Forward the socket of FORWARD, "SOCKET:127.0.0.1:PORT", to PORT of
   the loopback address, as ssh -N -L does.  Write the process to the
   file tunnels.  */
static int
fake_tunnel (const char *forward)
{
  char socket_name[128];
  const char *colon = strstr (forward, ":127.0.0.1:");
  if (!colon || colon - forward >= (long) sizeof socket_name)
    return 255;
  memcpy (socket_name, forward, colon - forward);
  socket_name[colon - forward] = 0;
  int port = atoi (colon + strlen (":127.0.0.1:"));
  char name[128];
  snprintf (name, sizeof name, "%s/tunnels", getenv ("RD_TEST_DIR"));
  FILE *file = fopen (name, "a");
  if (file)
    {
      fprintf (file, "%ld\n", (long) getpid ());
      fclose (file);
    }
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  snprintf (address.sun_path, sizeof address.sun_path, "%s", socket_name);
  int listener = socket (AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0
      || bind (listener, (struct sockaddr *) &address, sizeof address) != 0
      || listen (listener, 4) != 0)
    return 255;
  signal (SIGCHLD, SIG_IGN);
  for (;;)
    {
      int local = accept (listener, NULL, NULL);
      if (local < 0)
	continue;
      if (fork () == 0)
	{
	  prctl (PR_SET_PDEATHSIG, SIGKILL);
	  struct sockaddr_in remote = { .sin_family = AF_INET,
					.sin_port = htons (port),
					.sin_addr = { htonl (INADDR_LOOPBACK) } };
	  int fd = socket (AF_INET, SOCK_STREAM, 0);
	  if (fd >= 0
	      && connect (fd, (struct sockaddr *) &remote, sizeof remote) == 0)
	    splice_fds (local, fd);
	  _exit (0);
	}
      close (local);
    }
}

/* Copy the bytes between A and B until one of them ends.  */
static void
splice_fds (int a, int b)
{
  for (;;)
    {
      struct pollfd fds[2] = { { .fd = a, .events = POLLIN },
			       { .fd = b, .events = POLLIN } };
      if (poll (fds, 2, -1) < 0)
	return;
      for (int i = 0; i < 2; i++)
	if (fds[i].revents)
	  {
	    char bytes[4096];
	    ssize_t count = read (fds[i].fd, bytes, sizeof bytes);
	    if (count <= 0
		|| write (fds[1 - i].fd, bytes, count) != count)
	      return;
	  }
    }
}

/* Return the time of a monotonic clock in milliseconds.  */
static int64_t
now_ms (void)
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
