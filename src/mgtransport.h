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

#ifndef MGCLIENT_MGTRANSPORT_H
#define MGCLIENT_MGTRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdio.h>

#ifndef __EMSCRIPTEN__
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

#include "mgallocator.h"
#include "mgcommon.h"

typedef struct mg_transport {
  int (*send)(struct mg_transport *, const char *buf, size_t len);
  int (*recv)(struct mg_transport *, char *buf, size_t len);
  void (*destroy)(struct mg_transport *);
  void (*suspend_until_ready_to_read)(struct mg_transport *);
  void (*suspend_until_ready_to_write)(struct mg_transport *);
} mg_transport;

typedef struct mg_raw_transport {
  int (*send)(struct mg_transport *, const char *buf, size_t len);
  int (*recv)(struct mg_transport *, char *buf, size_t len);
  void (*destroy)(struct mg_transport *);
  void (*suspend_until_ready_to_read)(struct mg_transport *);
  void (*suspend_until_ready_to_write)(struct mg_transport *);
  int sockfd;
  mg_allocator *allocator;
} mg_raw_transport;

#ifndef __EMSCRIPTEN__
typedef struct mg_secure_transport {
  int (*send)(struct mg_transport *, const char *buf, size_t len);
  int (*recv)(struct mg_transport *, char *buf, size_t len);
  void (*destroy)(struct mg_transport *);
  void (*suspend_until_ready_to_read)(struct mg_transport *);
  void (*suspend_until_ready_to_write)(struct mg_transport *);
  SSL *ssl;
  BIO *bio;
  const char *peer_pubkey_type;
  char *peer_pubkey_fp;
  mg_allocator *allocator;
} mg_secure_transport;
#endif

int mg_transport_send(mg_transport *transport, const char *buf, size_t len);

int mg_transport_recv(mg_transport *transport, char *buf, size_t len);

void mg_transport_destroy(mg_transport *transport);

void mg_transport_suspend_until_ready_to_read(struct mg_transport *);

void mg_transport_suspend_until_ready_to_write(struct mg_transport *);

int mg_raw_transport_init(int sockfd, mg_raw_transport **transport,
                          mg_allocator *allocator);

int mg_raw_transport_send(struct mg_transport *, const char *buf, size_t len);

int mg_raw_transport_recv(struct mg_transport *, char *buf, size_t len);

void mg_raw_transport_destroy(struct mg_transport *);

void mg_raw_transport_suspend_until_ready_to_read(struct mg_transport *);

void mg_raw_transport_suspend_until_ready_to_write(struct mg_transport *);

#ifndef __EMSCRIPTEN__
// This function is mocked in tests during linking by using --wrap. ON_APPLE
// there is no --wrap. An alternative is to use -alias but if a symbol is
// strong linking fails.
MG_ATTRIBUTE_WEAK int mg_secure_transport_init(int sockfd,
                                               const char *cert_file,
                                               const char *key_file,
                                               mg_secure_transport **transport,
                                               mg_allocator *allocator);

int mg_secure_transport_send(mg_transport *, const char *buf, size_t len);

int mg_secure_transport_recv(mg_transport *, char *buf, size_t len);

void mg_secure_transport_destroy(mg_transport *);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MGCLIENT_MGTRANSPORT_H */
