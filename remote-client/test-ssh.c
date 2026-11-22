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

/* The test starts "emacs-remote-client --ssh-command=FAKE --ssh HOST".
   FAKE is this program under the name fake-ssh.  The test acts as the
   session, and the real relay of emacs-remote-client connects the
   session and the client.  The program shows windows, so the check of the
   Makefile runs this test in Xvfb.

   As "ssh -G", the fake writes a configuration whose host name is
   RD_TEST_HOSTNAME, or 127.0.0.1.  As the bootstrap, the fake runs the
   remote command with sh, with the loopback address as the server
   address of $SSH_CONNECTION.  A fake emacsclient gives the session
   socket of the test, and writes its arguments to the file eval of
   the run.  The fake keeps a copy of the line of the relay,
   so the test can connect to the relay, and stop it at the end.

   - Start: the client runs "ssh -G" and the bootstrap, and connects to
     the relay at the host name of "ssh -G", so the session gets a
     connection.  When the session removes its frame and closes, the
     client exits with the status 0.  SSH gets no open descriptor of
     the client other than its standard streams.
   - Reconnect: when another client with the token replaces the
     client, the client connects again, and the session gets its new
     connection.
   - Gone: when Emacs stops with no end of the session, the socket of
     the session stays, but refuses connections.  The client loses its
     connection, and connects to the relay again.  The relay cannot
     connect to the session, so it sends an end-session message that
     says that the session does not exist.  The client then exits with
     the status 1, and does not connect again.
   - Fail: when the bootstrap gives no line, the client exits with the
     status 1.
   - No host name: when "ssh -G" gives no host name, the client exits
     with the status 1, and does not run the bootstrap.
   - Unreachable: when the relay is not at the host name of "ssh -G",
     the client exits with the status 1.  The host name is a loopback
     address where the relay does not listen.
   - Connect: --connect=ADDRESS replaces the host name of "ssh -G".
   - Listen: with --listen=SPEC, the relay listens at the place of SPEC.
   - Bad listen: with a --listen that is not a spec, the client exits
     with the status 1, and does not run SSH.
   - Resume: with --session=NAME, the remote command asks Emacs to
     resume the session NAME, and the client connects to its socket.
   - Bad session: with a --session that is not a name of a session, the
     client exits with the status 1, and does not run SSH.
   - Usage: --ssh with no host, with a socket, or with --render-to, and
     --listen, --connect, or --session with no --ssh, make the program
     exit with the status 1.  */

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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "rdproto.h"
#include "relayline.h"
#include "stream.h"
#include "tls.h"

/* The longest wait for an event, in milliseconds.  The first connection
   also waits for the start of GTK.  */
enum { LIMIT = 10000 };

/* The timeout of the program in the examples, in milliseconds.  */
enum { TIMEOUT = 600 };

/* The frame of each example.  */
enum { FRAME = 7 };

/* The host name that "ssh -G" gives in the unreachable and connect
   examples.  The relay does not listen there.  */
#define ELSEWHERE "127.255.255.254"

/* One run of the program.  PID is 0 after the program exits; then
   STATUS is its exit status, or -1.  */
struct run
{
  char directory[64];
  char session[128];
  pid_t pid;
  int status;
  int listener;
  int connection;
};

/* How a run starts: the variables of the environment of the fake, as
   "NAME=VALUE" or NULL, and the extra options of the program, or
   NULL.  */
struct setup
{
  const char *environment[2];
  const char *options[2];
};

static bool start_example (void);
static bool reconnect_example (void);
static bool gone_example (void);
static bool fail_example (void);
static bool no_hostname_example (void);
static bool unreachable_example (void);
static bool connect_example (void);
static bool listen_example (void);
static bool bad_listen_example (void);
static bool resume_example (void);
static bool bad_session_example (void);
static bool usage_example (void);
static bool start_run (struct run *, struct setup);
static int accept_while_running (struct run *);
static bool refused_p (struct setup, int);
static void stop_run (struct run *);
static bool define_frame (int);
static bool end_session (struct run *);
static bool clean_after_exit (const struct run *);
static bool no_leaks_p (const struct run *, int);
static bool exists_p (const struct run *, const char *);
static int open_descriptors (void);
static bool read_relay_line (const struct run *, struct rc_relay_line *);
static struct rc_stream *connect_relay (const struct rc_relay_line *);
static void stop_relay (const struct run *);
static uint16_t free_port (void);
static bool make_bin (const struct run *);
static int accept_session (const struct run *);
static bool send_message (int, const struct rd_message *);
static int wait_for_exit (pid_t);
static int run_usage (const char *const *);
static int fake_ssh (int, char **);
static int fake_config (void);
static int fake_bootstrap (const char *);
static int64_t now_ms (void);

/* An example and its name.  */
static const struct
{
  const char *name;
  bool (*check) (void);
} examples[] =
  {
    { "start", start_example },
    { "reconnect", reconnect_example },
    { "gone", gone_example },
    { "fail", fail_example },
    { "no host name", no_hostname_example },
    { "unreachable", unreachable_example },
    { "connect", connect_example },
    { "listen", listen_example },
    { "bad listen", bad_listen_example },
    { "resume", resume_example },
    { "bad session", bad_session_example },
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
  if (!start_run (&run, (struct setup) { 0 }))
    return false;
  bool ok = end_session (&run) && clean_after_exit (&run)
    && no_leaks_p (&run, 2);
  stop_run (&run);
  return ok;
}

static bool
reconnect_example (void)
{
  struct run run;
  if (!start_run (&run, (struct setup) { 0 }))
    return false;
  struct rc_relay_line line;
  struct rc_stream *other
    = read_relay_line (&run, &line) ? connect_relay (&line) : NULL;
  /* The relay closes the connection of the client to the session, and
     opens one for OTHER.  Then the client connects again, and
     replaces OTHER.  */
  int for_other = other ? accept_session (&run) : -1;
  close (run.connection);
  run.connection = for_other >= 0 ? accept_session (&run) : -1;
  bool ok = (run.connection >= 0 && define_frame (run.connection)
	     && end_session (&run));
  rc_stream_close (other);
  if (for_other >= 0)
    close (for_other);
  stop_run (&run);
  return ok;
}

static bool
gone_example (void)
{
  struct run run;
  if (!start_run (&run, (struct setup) { 0 }))
    return false;
  close (run.listener);
  run.listener = -1;
  close (run.connection);
  run.connection = -1;
  bool ok = wait_for_exit (run.pid) == 1;
  run.pid = 0;
  stop_run (&run);
  return ok;
}

static bool
fail_example (void)
{
  return refused_p ((struct setup) { .environment = { "RD_TEST_FAIL=1" } },
		    2);
}

static bool
no_hostname_example (void)
{
  struct setup setup = { .environment = { "RD_TEST_HOSTNAME=" } };
  return refused_p (setup, 1);
}

static bool
unreachable_example (void)
{
  struct setup setup = { .environment = { "RD_TEST_HOSTNAME=" ELSEWHERE } };
  return refused_p (setup, 2);
}

static bool
connect_example (void)
{
  struct setup setup = { .environment = { "RD_TEST_HOSTNAME=" ELSEWHERE },
			 .options = { "--connect=127.0.0.1" } };
  struct run run;
  bool ok = start_run (&run, setup) && end_session (&run);
  stop_run (&run);
  return ok;
}

static bool
listen_example (void)
{
  uint16_t port = free_port ();
  char listen[64];
  snprintf (listen, sizeof listen, "--listen=127.0.0.1:%u",
	    (unsigned) port);
  struct run run;
  struct rc_relay_line line;
  bool ok = (port > 0
	     && start_run (&run, (struct setup) { .options = { listen } })
	     && read_relay_line (&run, &line) && line.port == port
	     && end_session (&run));
  stop_run (&run);
  return ok;
}

static bool
bad_listen_example (void)
{
  return refused_p ((struct setup) { .options = { "--listen=localhost" } },
		    0);
}

static bool
resume_example (void)
{
  struct run run;
  bool ok = (start_run (&run, (struct setup)
			{ .options = { "--session=remote-2" } })
	     && end_session (&run));
  char name[160];
  snprintf (name, sizeof name, "%s/eval", run.directory);
  char text[256] = "";
  FILE *file = fopen (name, "r");
  if (file)
    {
      text[fread (text, 1, sizeof text - 1, file)] = 0;
      fclose (file);
    }
  ok = ok && strstr (text, "(remote-display-resume \"remote-2\")");
  stop_run (&run);
  return ok;
}

static bool
bad_session_example (void)
{
  return refused_p ((struct setup) { .options = { "--session=a b" } }, 0);
}

static bool
usage_example (void)
{
  static const char *const no_host[] = { "--ssh", NULL };
  static const char *const socket[] = { "--ssh", "host", "socket", NULL };
  static const char *const png[] = { "--render-to=x.png", "--ssh", "host",
				     NULL };
  static const char *const listen[] = { "--listen=127.0.0.1", "socket",
					NULL };
  static const char *const connect[] = { "--connect=127.0.0.1", "socket",
					 NULL };
  static const char *const session[] = { "--session=remote-2", "socket",
					 NULL };
  return (run_usage (no_host) == 1 && run_usage (socket) == 1
	  && run_usage (png) == 1 && run_usage (listen) == 1
	  && run_usage (connect) == 1 && run_usage (session) == 1);
}

/* Make the directory of RUN, listen as the session, and start the
   program with SETUP.  Return true if the session gets a connection;
   then define a frame on it.  */
static bool
start_run (struct run *run, struct setup setup)
{
  *run = (struct run) { .listener = -1, .connection = -1, .status = -1 };
  strcpy (run->directory, "/tmp/test-ssh-XXXXXX");
  if (!mkdtemp (run->directory) || !make_bin (run))
    return false;
  snprintf (run->session, sizeof run->session, "%s/session",
	    run->directory);
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  if (snprintf (address.sun_path, sizeof address.sun_path, "%s",
		run->session) >= (int) sizeof address.sun_path)
    return false;
  /* The program and the relay must not get the listener: then it
     stays open after the close of the gone example.  */
  run->listener = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
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
      for (int i = 0; i < 2 && setup.environment[i]; i++)
	putenv ((char *) setup.environment[i]);
      const char *argv[8] = { "emacs-remote-client", timeout, ssh };
      int argc = 3;
      for (int i = 0; i < 2 && setup.options[i]; i++)
	argv[argc++] = setup.options[i];
      argv[argc++] = "--ssh";
      argv[argc++] = "testhost";
      execv ("./emacs-remote-client", (char **) argv);
      _exit (127);
    }
  run->connection = accept_while_running (run);
  return run->connection >= 0 && define_frame (run->connection);
}

/* Return the next connection of the session of RUN, or -1 if none
   comes in LIMIT milliseconds, or if the program exits first.  */
static int
accept_while_running (struct run *run)
{
  for (int64_t start = now_ms (); now_ms () - start < LIMIT; )
    {
      struct pollfd ready = { .fd = run->listener, .events = POLLIN };
      if (poll (&ready, 1, 10) == 1)
	return accept (run->listener, NULL, NULL);
      int status;
      if (waitpid (run->pid, &status, WNOHANG) == run->pid)
	{
	  run->pid = 0;
	  run->status = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
	  return -1;
	}
    }
  return -1;
}

/* Return true if the program with SETUP exits with the status 1, the
   session gets no connection, and the fake SSH runs RUNS times.  */
static bool
refused_p (struct setup setup, int runs)
{
  struct run run;
  bool started = start_run (&run, setup);
  bool ok = (!started && run.pid == 0 && run.status == 1
	     && (runs == 0 ? !exists_p (&run, "descriptors")
		 : no_leaks_p (&run, runs)));
  ok = ok && clean_after_exit (&run);
  stop_run (&run);
  return ok;
}

/* Send the definition of the frame of each example on FD.  Return true
   on success.  */
static bool
define_frame (int fd)
{
  struct rd_message define
    = { .type = RD_DEFINE_FRAME,
	.u.define_frame = { .frame = FRAME, .width = 200, .height = 100 } };
  return send_message (fd, &define);
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

/* Return true if the directory XDG_RUNTIME_DIR of RUN is empty.  */
static bool
clean_after_exit (const struct run *run)
{
  char runtime[96];
  snprintf (runtime, sizeof runtime, "%s/run", run->directory);
  return rmdir (runtime) == 0;
}

/* Return true if the fake SSH of RUN ran RUNS times, and each run got
   no descriptor other than its standard streams.  The fake writes the
   number of the other descriptors to the file descriptors.  */
static bool
no_leaks_p (const struct run *run, int runs)
{
  char name[128];
  snprintf (name, sizeof name, "%s/descriptors", run->directory);
  FILE *file = fopen (name, "r");
  int count, seen = 0;
  bool ok = file != NULL;
  while (ok && fscanf (file, "%d", &count) == 1)
    {
      seen++;
      ok = count == 0;
    }
  if (file)
    fclose (file);
  return ok && seen == runs;
}

/* Return true if the file NAME is in the directory of RUN.  */
static bool
exists_p (const struct run *run, const char *name)
{
  char path[160];
  snprintf (path, sizeof path, "%s/%s", run->directory, name);
  return access (path, F_OK) == 0;
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
  struct rc_relay_line line;
  struct rc_stream *stream
    = read_relay_line (run, &line) ? connect_relay (&line) : NULL;
  if (stream)
    {
      struct pollfd ready = { .fd = rc_stream_fd (stream),
			      .events = POLLIN };
      poll (&ready, 1, LIMIT);
    }
  rc_stream_close (stream);
}

/* Put the line of the relay of RUN, from the copy of the fake, in
   LINE.  Return false if there is no valid copy.  */
static bool
read_relay_line (const struct run *run, struct rc_relay_line *line)
{
  char name[128], text[RC_RELAY_LINE_SIZE] = "";
  snprintf (name, sizeof name, "%s/relay.line", run->directory);
  FILE *file = fopen (name, "r");
  bool found = (file && fgets (text, sizeof text, file)
		&& rc_relay_line_parse (text, line));
  if (file)
    fclose (file);
  return found;
}

/* Connect to the relay of LINE at the loopback address with TLS, and
   send its token.  Return the stream, or NULL.  */
static struct rc_stream *
connect_relay (const struct rc_relay_line *line)
{
  struct sockaddr_in address = { .sin_family = AF_INET,
				 .sin_port = htons (line->port),
				 .sin_addr = { htonl (INADDR_LOOPBACK) } };
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd >= 0
      && connect (fd, (struct sockaddr *) &address, sizeof address) != 0)
    {
      close (fd);
      fd = -1;
    }
  struct rc_stream *stream
    = fd < 0 ? NULL : rc_tls_connect (fd, line->pin, LIMIT);
  if (stream
      && rc_stream_write (stream, line->token, RC_TOKEN_SIZE)
	 != RC_TOKEN_SIZE)
    {
      rc_stream_close (stream);
      return NULL;
    }
  return stream;
}

/* Return a port of the loopback address that is free now, or 0.  */
static uint16_t
free_port (void)
{
  struct sockaddr_in address = { .sin_family = AF_INET,
				 .sin_addr = { htonl (INADDR_LOOPBACK) } };
  socklen_t size = sizeof address;
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  bool ok = (fd >= 0
	     && bind (fd, (struct sockaddr *) &address, sizeof address) == 0
	     && getsockname (fd, (struct sockaddr *) &address, &size) == 0);
  if (fd >= 0)
    close (fd);
  return ok ? ntohs (address.sin_port) : 0;
}

/* Make the directory bin of RUN: fake-ssh is this program,
   emacs-remote-client is the program of the test, and emacsclient
   prints the session socket as Emacs prints a string.  */
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
  snprintf (name, sizeof name, "%s/emacs-remote-client", bin);
  strcpy (target + length, "/emacs-remote-client");
  if (symlink (target, name) != 0)
    return false;
  snprintf (name, sizeof name, "%s/emacsclient", bin);
  FILE *file = fopen (name, "w");
  if (!file)
    return false;
  fputs ("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$RD_TEST_DIR/eval\"\n"
	 "printf '\"%s\"\\n' \"$RD_TEST_SESSION\"\n", file);
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

/* Run the program with ARGUMENTS, and return its exit status.  Return
   -2 if it does not tell the user on the standard error output to run
   --help, so another error with the status 1 does not pass for a usage
   error.  */
static int
run_usage (const char *const *arguments)
{
  int pipe_fds[2];
  if (pipe (pipe_fds) != 0)
    return -1;
  pid_t pid = fork ();
  if (pid == 0)
    {
      const char *argv[8] = { "emacs-remote-client" };
      for (int i = 0; arguments[i] && i < 6; i++)
	argv[i + 1] = arguments[i];
      dup2 (pipe_fds[1], STDERR_FILENO);
      close (pipe_fds[0]);
      execv ("./emacs-remote-client", (char **) argv);
      _exit (127);
    }
  close (pipe_fds[1]);
  char text[2048];
  size_t used = 0;
  ssize_t n;
  while (used < sizeof text - 1
	 && (n = read (pipe_fds[0], text + used, sizeof text - 1 - used)) > 0)
    used += n;
  text[used] = 0;
  close (pipe_fds[0]);
  int status = wait_for_exit (pid);
  return (strstr (text, "Try 'emacs-remote-client --help' for more "
		  "information")
	  ? status : -2);
}

/* The fake SSH.  With -G, it writes a configuration.  Otherwise, it
   runs the last argument with sh.  */
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
  if (argc == 4 && strcmp (argv[1], "-G") == 0
      && strcmp (argv[2], "--") == 0)
    return fake_config ();
  if (argc < 4 || strcmp (argv[argc - 3], "--") != 0)
    return 255;
  if (getenv ("RD_TEST_FAIL"))
    return 255;
  setenv ("SSH_CONNECTION", "127.0.0.2 40000 127.0.0.1 22", 1);
  return fake_bootstrap (argv[argc - 1]);
}

/* Write a configuration as "ssh -G" does.  Its host name is
   RD_TEST_HOSTNAME, or 127.0.0.1.  An empty RD_TEST_HOSTNAME gives no
   host name line.  */
static int
fake_config (void)
{
  const char *hostname = getenv ("RD_TEST_HOSTNAME");
  printf ("user test\n");
  if (!hostname || *hostname)
    printf ("hostname %s\n", hostname ? hostname : "127.0.0.1");
  printf ("port 22\naddressfamily any\n");
  return 0;
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

/* Return the time of a monotonic clock in milliseconds.  */
static int64_t
now_ms (void)
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
