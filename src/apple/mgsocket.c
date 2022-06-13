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

#include "mgsocket.h"

#include <string.h>

#include "mgcommon.h"

#define MG_RETRY_ON_EINTR(expression)          \
  __extension__({                              \
    long result;                               \
    do {                                       \
      result = (long)(expression);             \
    } while (result == -1L && errno == EINTR); \
    result;                                    \
  })

int mg_socket_init() { return MG_SUCCESS; }

int mg_socket_create(int af, int type, int protocol) {
  int sockfd = socket(af, type, protocol);
  if (sockfd == -1) {
    return MG_ERROR_SOCKET;
  }
  return sockfd;
}

int mg_socket_create_handle_error(int sock, mg_session *session) {
  if (sock == MG_ERROR_SOCKET) {
    mg_session_set_error(session, "couldn't open socket: %s",
                         mg_socket_error());
    return MG_ERROR_NETWORK_FAILURE;
  }
  return MG_SUCCESS;
}

int mg_socket_connect(int sock, const struct sockaddr *addr,
                      socklen_t addrlen) {
  long status = MG_RETRY_ON_EINTR(connect(sock, addr, addrlen));
  if (status == -1L) {
    return MG_ERROR_SOCKET;
  }
  return MG_SUCCESS;
}

int mg_socket_connect_handle_error(int *sock, int status, mg_session *session) {
  if (status != MG_SUCCESS) {
    mg_session_set_error(session, "couldn't connect to host: %s",
                         mg_socket_error());
    if (mg_socket_close(*sock) != 0) {
      abort();
    }
    *sock = MG_ERROR_SOCKET;
    return MG_ERROR_NETWORK_FAILURE;
  }
  return MG_SUCCESS;
}

int mg_socket_options(int sock, mg_session *session) {
  (void)sock;
  (void)session;
  return MG_SUCCESS;
}

ssize_t mg_socket_send(int sock, const void *buf, int len) {
  return MG_RETRY_ON_EINTR(send(sock, buf, len, 0));
}

ssize_t mg_socket_receive(int sock, void *buf, int len) {
  return MG_RETRY_ON_EINTR(recv(sock, buf, len, 0));
}

int mg_socket_poll(struct pollfd *fds, unsigned int nfds, int timeout) {
  return MG_RETRY_ON_EINTR(poll(fds, nfds, timeout));
}

int mg_socket_pair(int d, int type, int protocol, int *sv) {
  return socketpair(d, type, protocol, sv);
}

int mg_socket_close(int sock) { return MG_RETRY_ON_EINTR(close(sock)); }

char *mg_socket_error() { return strerror(errno); }

void mg_socket_finalize() {}
