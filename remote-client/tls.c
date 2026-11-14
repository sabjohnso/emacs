/* The TLS streams of remote-client.


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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "streamclass.h"
#include "tls.h"

/* Only TLS 1.3.  */
#define PRIORITY "NORMAL:-VERS-ALL:+VERS-TLS1.3"

/* The life of a certificate in seconds.  The client checks only the
   pin, but a certificate must have a life.  */
enum { CERTIFICATE_LIFE = 10 * 365 * 24 * 3600 };

struct rc_tls_key
{
  gnutls_x509_privkey_t private_key;
  gnutls_x509_crt_t certificate;
  gnutls_certificate_credentials_t credentials;
  uint8_t pin[RC_PIN_SIZE];
};

/* A TLS stream.  CREDENTIALS are the credentials of a client, which
   the stream owns, or NULL for a server.  SHAKEN is true after the
   handshake.  */
struct tls_stream
{
  struct rc_stream stream;
  gnutls_session_t session;
  gnutls_certificate_credentials_t credentials;
  bool shaken;
};

static bool make_certificate (struct rc_tls_key *);
static bool public_key_pin (gnutls_pubkey_t, uint8_t *);
static struct tls_stream *new_stream (int, unsigned int,
				      gnutls_certificate_credentials_t);
static bool server_pin_p (gnutls_session_t, const uint8_t *);
static ptrdiff_t read_tls (struct rc_stream *, void *, size_t);
static ptrdiff_t write_tls (struct rc_stream *, const void *, size_t);
static short tls_events (const struct rc_stream *, short);
static bool tls_buffered_p (const struct rc_stream *);
static void close_tls (struct rc_stream *);
static void free_stream (struct tls_stream *, bool);
static int shake (struct tls_stream *);
static ptrdiff_t tls_result (ssize_t);

/* The kind of a TLS stream.  */
static const struct rc_stream_class tls_class =
  { read_tls, write_tls, tls_events, tls_buffered_p, close_tls };

struct rc_tls_key *
rc_tls_key_new (void)
{
  struct rc_tls_key *key = calloc (1, sizeof *key);
  if (!key)
    return NULL;
  if (gnutls_x509_privkey_init (&key->private_key) < 0)
    {
      free (key);
      return NULL;
    }
  if (gnutls_x509_crt_init (&key->certificate) < 0)
    key->certificate = NULL;
  if (gnutls_certificate_allocate_credentials (&key->credentials) < 0)
    key->credentials = NULL;
  if (!key->certificate || !key->credentials || !make_certificate (key))
    {
      rc_tls_key_free (key);
      return NULL;
    }
  return key;
}

void
rc_tls_key_pin (const struct rc_tls_key *key, uint8_t pin[RC_PIN_SIZE])
{
  memcpy (pin, key->pin, RC_PIN_SIZE);
}

void
rc_tls_key_free (struct rc_tls_key *key)
{
  if (!key)
    return;
  if (key->credentials)
    gnutls_certificate_free_credentials (key->credentials);
  if (key->certificate)
    gnutls_x509_crt_deinit (key->certificate);
  gnutls_x509_privkey_deinit (key->private_key);
  free (key);
}

struct rc_stream *
rc_tls_accept (const struct rc_tls_key *key, int fd)
{
  struct tls_stream *tls
    = new_stream (fd, GNUTLS_SERVER | GNUTLS_NONBLOCK | GNUTLS_NO_TICKETS,
		  NULL);
  if (!tls)
    return NULL;
  if (gnutls_credentials_set (tls->session, GNUTLS_CRD_CERTIFICATE,
			      key->credentials) < 0)
    {
      free_stream (tls, false);
      return NULL;
    }
  return &tls->stream;
}

struct rc_stream *
rc_tls_connect (int fd, const uint8_t pin[RC_PIN_SIZE], int limit)
{
  gnutls_certificate_credentials_t credentials;
  if (gnutls_certificate_allocate_credentials (&credentials) < 0)
    {
      close (fd);
      return NULL;
    }
  struct tls_stream *tls = new_stream (fd, GNUTLS_CLIENT, credentials);
  if (!tls)
    return NULL;
  gnutls_handshake_set_timeout (tls->session, limit);
  /* The socket blocks, and GnuTLS goes on after a signal in a
     handshake with a time limit, so the handshake does not wait.  */
  if (gnutls_credentials_set (tls->session, GNUTLS_CRD_CERTIFICATE,
			      credentials) < 0
      || shake (tls) <= 0 || !server_pin_p (tls->session, pin))
    {
      free_stream (tls, false);
      return NULL;
    }
  return &tls->stream;
}

/* Make a new Ed25519 key and its self-signed certificate in KEY, put
   them in the credentials of KEY, and compute the pin.  Return false
   on an error.  */
static bool
make_certificate (struct rc_tls_key *key)
{
  static const unsigned char serial[] = { 1 };
  time_t now = time (NULL);
  gnutls_pubkey_t public_key;
  if (gnutls_pubkey_init (&public_key) < 0)
    return false;
  bool ok = (gnutls_x509_privkey_generate (key->private_key,
					   GNUTLS_PK_EDDSA_ED25519, 0, 0) >= 0
	     && gnutls_x509_crt_set_version (key->certificate, 3) >= 0
	     && gnutls_x509_crt_set_serial (key->certificate, serial,
					    sizeof serial) >= 0
	     && gnutls_x509_crt_set_activation_time (key->certificate,
						     now - 3600) >= 0
	     && gnutls_x509_crt_set_expiration_time (key->certificate,
						     now + CERTIFICATE_LIFE)
		>= 0
	     && gnutls_x509_crt_set_key (key->certificate,
					 key->private_key) >= 0
	     && gnutls_x509_crt_sign2 (key->certificate, key->certificate,
				       key->private_key, GNUTLS_DIG_UNKNOWN,
				       0) >= 0
	     && gnutls_certificate_set_x509_key (key->credentials,
						 &key->certificate, 1,
						 key->private_key) >= 0
	     && gnutls_pubkey_import_x509 (public_key, key->certificate, 0)
		>= 0
	     && public_key_pin (public_key, key->pin));
  gnutls_pubkey_deinit (public_key);
  return ok;
}

/* Put the pin of PUBLIC_KEY in PIN.  Return false on an error.  */
static bool
public_key_pin (gnutls_pubkey_t public_key, uint8_t *pin)
{
  gnutls_datum_t der;
  if (gnutls_pubkey_export2 (public_key, GNUTLS_X509_FMT_DER, &der) < 0)
    return false;
  bool ok = gnutls_hash_fast (GNUTLS_DIG_SHA256, der.data, der.size,
			      pin) >= 0;
  gnutls_free (der.data);
  return ok;
}

/* Return a new TLS stream on the socket FD with the flags FLAGS of
   gnutls_init and the CREDENTIALS of a client, or NULL.  The stream
   owns FD and CREDENTIALS.  On an error, close FD, and free
   CREDENTIALS.  */
static struct tls_stream *
new_stream (int fd, unsigned int flags,
	    gnutls_certificate_credentials_t credentials)
{
  struct tls_stream *tls = malloc (sizeof *tls);
  if (!tls)
    {
      close (fd);
      if (credentials)
	gnutls_certificate_free_credentials (credentials);
      return NULL;
    }
  *tls = (struct tls_stream) { { &tls_class, fd }, NULL, credentials,
			       false };
  if (gnutls_init (&tls->session, flags | GNUTLS_NO_SIGNAL) < 0)
    {
      tls->session = NULL;
      free_stream (tls, false);
      return NULL;
    }
  if (gnutls_priority_set_direct (tls->session, PRIORITY, NULL) < 0)
    {
      free_stream (tls, false);
      return NULL;
    }
  gnutls_transport_set_int (tls->session, fd);
  return tls;
}

/* Return true if the key of the certificate of the server of SESSION
   has the pin PIN.  */
static bool
server_pin_p (gnutls_session_t session, const uint8_t *pin)
{
  unsigned int count;
  const gnutls_datum_t *certificates
    = gnutls_certificate_get_peers (session, &count);
  gnutls_pubkey_t public_key;
  if (!certificates || count < 1 || gnutls_pubkey_init (&public_key) < 0)
    return false;
  uint8_t got[RC_PIN_SIZE];
  bool ok = (gnutls_pubkey_import_x509_raw (public_key, &certificates[0],
					    GNUTLS_X509_FMT_DER, 0) >= 0
	     && public_key_pin (public_key, got)
	     && memcmp (got, pin, RC_PIN_SIZE) == 0);
  gnutls_pubkey_deinit (public_key);
  return ok;
}

/* The read function of a TLS stream.  */
static ptrdiff_t
read_tls (struct rc_stream *stream, void *bytes, size_t size)
{
  struct tls_stream *tls = (struct tls_stream *) stream;
  int shaken = shake (tls);
  if (shaken <= 0)
    return shaken < 0 ? RC_STREAM_FAIL : RC_STREAM_WAIT;
  return tls_result (gnutls_record_recv (tls->session, bytes, size));
}

/* The write function of a TLS stream.  */
static ptrdiff_t
write_tls (struct rc_stream *stream, const void *bytes, size_t size)
{
  struct tls_stream *tls = (struct tls_stream *) stream;
  int shaken = shake (tls);
  if (shaken <= 0)
    return shaken < 0 ? RC_STREAM_FAIL : RC_STREAM_WAIT;
  return tls_result (gnutls_record_send (tls->session, bytes, size));
}

/* The events function of a TLS stream: in the handshake, the
   direction of the handshake.  */
static short
tls_events (const struct rc_stream *stream, short events)
{
  const struct tls_stream *tls = (const struct tls_stream *) stream;
  if (tls->shaken)
    return events;
  return gnutls_record_get_direction (tls->session) ? POLLOUT : POLLIN;
}

/* The buffered function of a TLS stream: GnuTLS holds the rest of a
   record after a short read.  */
static bool
tls_buffered_p (const struct rc_stream *stream)
{
  const struct tls_stream *tls = (const struct tls_stream *) stream;
  return gnutls_record_check_pending (tls->session) > 0;
}

/* The close function of a TLS stream.  */
static void
close_tls (struct rc_stream *stream)
{
  struct tls_stream *tls = (struct tls_stream *) stream;
  free_stream (tls, tls->shaken);
}

/* Close the socket of TLS, and free TLS.  If BYE, first tell the peer
   that the stream ends, so the peer reads an end and not an error.  A
   socket that does not block can refuse the alert of the end; then the
   peer reads an error.  */
static void
free_stream (struct tls_stream *tls, bool bye)
{
  if (bye)
    gnutls_bye (tls->session, GNUTLS_SHUT_WR);
  if (tls->session)
    gnutls_deinit (tls->session);
  if (tls->credentials)
    gnutls_certificate_free_credentials (tls->credentials);
  close (tls->stream.fd);
  free (tls);
}

/* Continue the handshake of TLS.  Return 1 after the handshake, 0 if
   it waits, or -1 if it failed.  */
static int
shake (struct tls_stream *tls)
{
  if (tls->shaken)
    return 1;
  int result = gnutls_handshake (tls->session);
  if (result == GNUTLS_E_SUCCESS)
    {
      tls->shaken = true;
      return 1;
    }
  return gnutls_error_is_fatal (result) ? -1 : 0;
}

/* Return the result of a stream for the result COUNT of
   gnutls_record_recv or gnutls_record_send.  */
static ptrdiff_t
tls_result (ssize_t count)
{
  if (count >= 0)
    return count;
  if (count == GNUTLS_E_AGAIN || count == GNUTLS_E_INTERRUPTED)
    return RC_STREAM_WAIT;
  return RC_STREAM_FAIL;
}
