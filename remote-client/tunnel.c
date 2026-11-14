/* The SSH tunnel of remote-client to a relay.

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

#include <config.h>

#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "child.h"
#include "clock.h"
#include "sshargs.h"
#include "tunnel.h"
#include "unixsocket.h"

/* The wait between two tries of the first connection.  */
enum { FIRST_WAIT = 50 };

/* The most bytes of the name of a Unix-domain socket, with its null
   byte.  */
enum { SOCKET_SIZE = sizeof ((struct sockaddr_un *) 0)->sun_path };

/* The state of SSH after a check.  */
enum ssh_state
  {
    /* SSH runs, so the local socket can exist.  */
    SSH_RUNNING,
    /* SSH did not run, and started now.  */
    SSH_STARTED,
    /* SSH did not run, and cannot start.  */
    SSH_FAILED,
  };

/* A tunnel.  PID is the process of SSH, or 0 if SSH does not run.  */
struct rc_tunnel
{
  const char *ssh;
  const char *host;
  struct rc_relay_line line;
  char directory[SOCKET_SIZE];
  char socket[SOCKET_SIZE];
  pid_t pid;
};

static enum ssh_state ensure_running (struct rc_tunnel *);
static int connect_once (const struct rc_tunnel *);
static bool running_p (struct rc_tunnel *);
static bool start (struct rc_tunnel *);

struct rc_tunnel *
rc_tunnel_new (const char *ssh, const char *host,
	       const struct rc_relay_line *line, const char *base)
{
  struct rc_tunnel *tunnel = calloc (1, sizeof *tunnel);
  if (!tunnel)
    return NULL;
  int length = snprintf (tunnel->directory, sizeof tunnel->directory,
			 "%s/remote-client-XXXXXX", base);
  if (length < 0
      || (size_t) length + strlen ("/socket") >= sizeof tunnel->socket)
    {
      fprintf (stderr, "remote-client: %s: the name is too long\n", base);
      free (tunnel);
      return NULL;
    }
  if (!mkdtemp (tunnel->directory))
    {
      perror ("remote-client: mkdtemp");
      free (tunnel);
      return NULL;
    }
  snprintf (tunnel->socket, sizeof tunnel->socket, "%s/socket",
	    tunnel->directory);
  tunnel->ssh = ssh;
  tunnel->host = host;
  tunnel->line = *line;
  return tunnel;
}

int
rc_tunnel_first_connect (struct rc_tunnel *tunnel)
{
  int64_t end = rc_now_ms () + RC_TUNNEL_FIRST_LIMIT;
  int starts = 0;
  while (rc_now_ms () < end)
    {
      enum ssh_state state = ensure_running (tunnel);
      if (state == SSH_FAILED
	  || (state == SSH_STARTED && ++starts > 1))
	break;
      int fd = state == SSH_RUNNING ? connect_once (tunnel) : -1;
      if (fd >= 0)
	return fd;
      poll (NULL, 0, FIRST_WAIT);
    }
  fprintf (stderr, "remote-client: %s: the tunnel did not connect\n",
	   tunnel->host);
  return -1;
}

int
rc_tunnel_connect (void *data)
{
  struct rc_tunnel *tunnel = data;
  if (ensure_running (tunnel) != SSH_RUNNING)
    return -1;
  return connect_once (tunnel);
}

void
rc_tunnel_free (struct rc_tunnel *tunnel)
{
  if (!tunnel)
    return;
  if (running_p (tunnel))
    {
      kill (tunnel->pid, SIGTERM);
      waitpid (tunnel->pid, NULL, 0);
    }
  unlink (tunnel->socket);
  rmdir (tunnel->directory);
  free (tunnel);
}

/* Start the SSH of TUNNEL if it does not run, and return the state.  */
static enum ssh_state
ensure_running (struct rc_tunnel *tunnel)
{
  if (running_p (tunnel))
    return SSH_RUNNING;
  return start (tunnel) ? SSH_STARTED : SSH_FAILED;
}

/* Return a new connection to the local socket of TUNNEL, after the
   token of the relay, or -1.  */
static int
connect_once (const struct rc_tunnel *tunnel)
{
  int fd = rc_connect_unix (tunnel->socket, false);
  if (fd < 0)
    return -1;
  if (send (fd, tunnel->line.token, RC_TOKEN_SIZE, MSG_NOSIGNAL)
      != RC_TOKEN_SIZE)
    {
      close (fd);
      return -1;
    }
  return fd;
}

/* Return true if the SSH of TUNNEL runs.  */
static bool
running_p (struct rc_tunnel *tunnel)
{
  if (tunnel->pid > 0 && waitpid (tunnel->pid, NULL, WNOHANG) != 0)
    tunnel->pid = 0;
  return tunnel->pid > 0;
}

/* Start the SSH of TUNNEL, and return true if it started.  SSH makes
   the local socket, so remove an old one first.  */
static bool
start (struct rc_tunnel *tunnel)
{
  const char *args[RC_SSH_ARGS_SIZE];
  char forward[RC_SSH_FORWARD_SIZE];
  if (rc_ssh_tunnel_args (tunnel->ssh, tunnel->host, tunnel->socket,
			  tunnel->line.port, forward, sizeof forward,
			  args) < 0)
    return false;
  unlink (tunnel->socket);
  pid_t pid = fork ();
  if (pid == 0)
    rc_child_exec (tunnel->ssh, args);
  tunnel->pid = pid > 0 ? pid : 0;
  return pid > 0;
}
