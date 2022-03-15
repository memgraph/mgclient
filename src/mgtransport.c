// Copyright (c) 2016-2020 Memgraph Ltd. [https://memgraph.com]
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mgtransport.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef MGCLIENT_ON_LINUX
#ifndef __EMSCRIPTEN__
#include <pthread.h>
#endif
#endif  // MGCLIENT_ON_LINUX

#include "mgallocator.h"
#include "mgclient.h"
#include "mgcommon.h"
#include "mgsocket.h"
#ifdef __EMSCRIPTEN__
#include "mgwasm.h"
#endif

int mg_init_ssl = 1;

int mg_transport_send(mg_transport *transport, const char *buf, size_t len) {
  return transport->send(transport, buf, len);
}

int mg_transport_recv(mg_transport *transport, char *buf, size_t len) {
  return transport->recv(transport, buf, len);
}

void mg_transport_destroy(mg_transport *transport) {
  transport->destroy(transport);
}

void mg_transport_suspend_until_ready_to_read(struct mg_transport *transport) {
  if (transport->suspend_until_ready_to_read) {
    transport->suspend_until_ready_to_read(transport);
  }
}

void mg_transport_suspend_until_ready_to_write(struct mg_transport *transport) {
  if (transport->suspend_until_ready_to_write) {
    transport->suspend_until_ready_to_write(transport);
  }
}

int mg_raw_transport_init(int sockfd, mg_raw_transport **transport,
                          mg_allocator *allocator) {
  mg_raw_transport *ttransport =
      mg_allocator_malloc(allocator, sizeof(mg_raw_transport));
  if (!ttransport) {
    return MG_ERROR_OOM;
  }
  ttransport->sockfd = sockfd;
  ttransport->send = mg_raw_transport_send;
  ttransport->recv = mg_raw_transport_recv;
  ttransport->destroy = mg_raw_transport_destroy;
  ttransport->suspend_until_ready_to_read =
      mg_raw_transport_suspend_until_ready_to_read;
  ttransport->suspend_until_ready_to_write =
      mg_raw_transport_suspend_until_ready_to_write;
  ttransport->allocator = allocator;
  *transport = ttransport;
  return 0;
}

int mg_raw_transport_send(struct mg_transport *transport, const char *buf,
                          size_t len) {
  int sockfd = ((mg_raw_transport *)transport)->sockfd;
  size_t total_sent = 0;
  while (total_sent < len) {
    // TODO(mtomic): maybe enable using MSG_MORE here
    ssize_t sent_now =
        mg_socket_send(sockfd, buf + total_sent, len - total_sent);
    if (sent_now == -1) {
      perror("mg_raw_transport_send");
      return -1;
    }
    total_sent += (size_t)sent_now;
  }
  return 0;
}

int mg_raw_transport_recv(struct mg_transport *transport, char *buf,
                          size_t len) {
  int sockfd = ((mg_raw_transport *)transport)->sockfd;
  size_t total_received = 0;
  while (total_received < len) {
    ssize_t received_now =
        mg_socket_receive(sockfd, buf + total_received, len - total_received);
    if (received_now == 0) {
      // Server closed the connection.
      fprintf(stderr, "mg_raw_transport_recv: connection closed by server\n");
      return -1;
    }
    if (received_now == -1) {
      perror("mg_raw_transport_recv");
      return -1;
    }
    total_received += (size_t)received_now;
  }
  return 0;
}

void mg_raw_transport_destroy(struct mg_transport *transport) {
  mg_raw_transport *self = (mg_raw_transport *)transport;
  if (mg_socket_close(self->sockfd) != 0) {
    abort();
  }
  mg_allocator_free(self->allocator, transport);
}

void mg_raw_transport_suspend_until_ready_to_read(
    struct mg_transport *transport) {
#ifdef __EMSCRIPTEN__
  const int sock = ((mg_raw_transport *)transport)->sockfd;
  mg_wasm_suspend_until_ready_to_read(sock);
#else
  (void)transport;
#endif
}

void mg_raw_transport_suspend_until_ready_to_write(
    struct mg_transport *transport) {
#ifdef __EMSCRIPTEN__
  const int sock = ((mg_raw_transport *)transport)->sockfd;
  mg_wasm_suspend_until_ready_to_write(sock);
#else
  (void)transport;
#endif
}

#ifndef __EMSCRIPTEN__
static int print_ssl_error(const char *str, size_t len, void *u) {
  (void)len;
  fprintf(stderr, "%s: %s", (char *)u, str);
  return 0;
}

static char *hex_encode(unsigned char *data, unsigned int len,
                        mg_allocator *allocator) {
  char *encoded = mg_allocator_malloc(allocator, 2 * len + 1);
  for (unsigned int i = 0; i < len; ++i) {
    sprintf(encoded + 2 * i, "%02x", data[i]);
  }
  return encoded;
}

static void mg_openssl_init() {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  static int mg_ssl_initialized = 0;
  pthread_mutex_lock(&mutex);
  if (mg_init_ssl && !mg_ssl_initialized) {
    printf("initializing openssl\n");
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_crypto_strings();
    mg_ssl_initialized = 1;
  }
  pthread_mutex_unlock(&mutex);
#endif
}

int mg_secure_transport_init(int sockfd, const char *cert_file,
                             const char *key_file,
                             mg_secure_transport **transport,
                             mg_allocator *allocator) {
  mg_openssl_init();

  SSL_CTX *ctx = NULL;
  SSL *ssl = NULL;
  BIO *bio = NULL;

  int status = 0;

  ERR_clear_error();

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  ctx = SSL_CTX_new(SSLv23_client_method());
#else
  ctx = SSL_CTX_new(TLS_client_method());
#endif

  if (!ctx) {
    status = MG_ERROR_SSL_ERROR;
    goto failure;
  }

  if (cert_file && key_file) {
    if (SSL_CTX_use_certificate_chain_file(ctx, cert_file) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1) {
      status = MG_ERROR_SSL_ERROR;
      goto failure;
    }
  }

  SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3);
  ssl = SSL_new(ctx);

  if (!ssl) {
    status = MG_ERROR_SSL_ERROR;
    goto failure;
  }

  // SSL_CTX object is reference counted, we're destroying this local reference,
  // but reference from SSL object stays.
  SSL_CTX_free(ctx);
  ctx = NULL;

  bio = BIO_new_socket(sockfd, BIO_NOCLOSE);
  if (!bio) {
    status = MG_ERROR_SSL_ERROR;
    goto failure;
  }
  SSL_set_bio(ssl, bio, bio);

  int ret = SSL_connect(ssl);
  if (ret < 0) {
    status = MG_ERROR_SSL_ERROR;
    goto failure;
  }

  // Get server's public key type and fingerprint.
  X509 *peer_cert = SSL_get_peer_certificate(ssl);
  assert(peer_cert);
  EVP_PKEY *peer_pubkey = X509_get_pubkey(peer_cert);
  int nid = EVP_PKEY_base_id(peer_pubkey);
  EVP_PKEY_free(peer_pubkey);
  const char *peer_pubkey_type =
      (nid == NID_undef) ? "UNKNOWN" : OBJ_nid2ln(nid);
  unsigned char peer_pubkey_fp[EVP_MAX_MD_SIZE];
  unsigned int peer_pubkey_fp_len;
  if (X509_pubkey_digest(peer_cert, EVP_sha512(), peer_pubkey_fp,
                         &peer_pubkey_fp_len) != 1) {
    status = MG_ERROR_SSL_ERROR;
    X509_free(peer_cert);
    goto failure;
  }
  X509_free(peer_cert);

  mg_secure_transport *ttransport =
      mg_allocator_malloc(allocator, sizeof(mg_secure_transport));
  if (!ttransport) {
    status = MG_ERROR_OOM;
    goto failure;
  }

  // Take ownership of the socket now that everything went well.
  BIO_set_close(bio, BIO_CLOSE);

  ttransport->ssl = ssl;
  ttransport->bio = bio;
  ttransport->peer_pubkey_type = peer_pubkey_type;
  ttransport->peer_pubkey_fp =
      hex_encode(peer_pubkey_fp, peer_pubkey_fp_len, allocator);
  ttransport->send = mg_secure_transport_send;
  ttransport->recv = mg_secure_transport_recv;
  ttransport->suspend_until_ready_to_read = NULL;
  ttransport->suspend_until_ready_to_write = NULL;
  ttransport->destroy = mg_secure_transport_destroy;
  ttransport->allocator = allocator;
  *transport = ttransport;

  return 0;

failure:
  if (status == MG_ERROR_SSL_ERROR) {
    ERR_print_errors_cb(print_ssl_error, "mg_secure_transport_init");
  }
  SSL_CTX_free(ctx);
  if (ssl) {
    // If SSL object was successfuly created, it owns the BIO so we don't need
    // to destroy it.
    SSL_free(ssl);
  } else {
    BIO_free(bio);
  }

  return status;
}

int mg_secure_transport_send(mg_transport *transport, const char *buf,
                             size_t len) {
  SSL *ssl = ((mg_secure_transport *)transport)->ssl;
  BIO *bio = ((mg_secure_transport *)transport)->bio;
  size_t total_sent = 0;
  while (total_sent < len) {
    ERR_clear_error();
    int sent_now = SSL_write(ssl, buf + total_sent, (int)(len - total_sent));
    if (sent_now <= 0) {
      int err = SSL_get_error(ssl, sent_now);
      if (err == SSL_ERROR_WANT_READ) {
        struct pollfd p;
        if (BIO_get_fd(bio, &p.fd) < 0) {
          abort();
        }
        p.events = POLLIN;
        if (mg_socket_poll(&p, 1, -1) < 0) {
          return -1;
        }
        continue;
      } else {
        ERR_print_errors_cb(print_ssl_error, "mg_secure_transport_send");
        return -1;
      }
    }
    assert((size_t)sent_now == len);
    total_sent += (size_t)sent_now;
  }
  return 0;
}

int mg_secure_transport_recv(mg_transport *transport, char *buf, size_t len) {
  SSL *ssl = ((mg_secure_transport *)transport)->ssl;
  BIO *bio = ((mg_secure_transport *)transport)->bio;
  size_t total_received = 0;
  while (total_received < len) {
    ERR_clear_error();
    int received_now =
        SSL_read(ssl, buf + total_received, (int)(len - total_received));
    if (received_now <= 0) {
      int err = SSL_get_error(ssl, received_now);
      if (err == SSL_ERROR_WANT_READ) {
        struct pollfd p;
        if (BIO_get_fd(bio, &p.fd) < 0) {
          abort();
        }
        p.events = POLLIN;
        if (mg_socket_poll(&p, 1, -1) < 0) {
          return -1;
        }
        continue;
      } else {
        ERR_print_errors_cb(print_ssl_error, "mg_secure_transport_recv");
        return -1;
      }
    }
    total_received += (size_t)received_now;
  }
  return 0;
}

void mg_secure_transport_destroy(mg_transport *transport) {
  mg_secure_transport *self = (mg_secure_transport *)transport;
  SSL_free(self->ssl);
  self->bio = NULL;
  self->ssl = NULL;
  mg_allocator_free(self->allocator, self->peer_pubkey_fp);
  mg_allocator_free(self->allocator, self);
}
#endif
