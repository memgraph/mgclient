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

#include <netinet/tcp.h>
#include <string.h>

#include "mgclient-error.h"
#include "mgcommon.h"

int mg_socket_init() { return 0; }

int mg_socket_create(int af, int type, int protocol) {
  return socket(af, type, protocol);
}

int mg_socket_connect(int sock, const struct sockaddr *addr,
                      socklen_t addrlen) {
  return MG_RETRY_ON_EINTR(connect(sock, addr, addrlen));
}

int mg_socket_options(int sock, mg_session *session) {
  struct {
    int level;
    int optname;
    int optval;
  } socket_options[] = {// disable Nagle algorithm for performance reasons
                        {SOL_TCP, TCP_NODELAY, 1},
                        // turn keep-alive on
                        {SOL_SOCKET, SO_KEEPALIVE, 1},
                        // wait 20s before sending keep-alive packets
                        {SOL_TCP, TCP_KEEPIDLE, 20},
                        // 4 keep-alive packets must fail to close
                        {SOL_TCP, TCP_KEEPCNT, 4},
                        // send keep-alive packets every 15s
                        {SOL_TCP, TCP_KEEPINTVL, 15}};
  const size_t OPTCNT = sizeof(socket_options) / sizeof(socket_options[0]);

  for (size_t i = 0; i < OPTCNT; ++i) {
    int optval = socket_options[i].optval;
    socklen_t optlen = sizeof(optval);

    if (setsockopt(sock, socket_options[i].level, socket_options[i].optname,
                   (void *)&optval, optlen) != 0) {
      mg_session_set_error(session, "couldn't set socket option: %s",
                           mg_socket_error());
      if (mg_socket_close(sock) != 0) {
        abort();
      }
      return MG_ERROR_NETWORK_FAILURE;
    }
  }
  return 0;
}

int mg_socket_send(int sock, const void *buf, int len) {
  return (int)send(sock, buf, len, MSG_NOSIGNAL);
}

int mg_socket_receive(int sock, void *buf, int len) {
  return (int)recv(sock, buf, len, 0);
}

int mg_socket_pair(int d, int type, int protocol, int *sv) {
  return socketpair(d, type, protocol, sv);
}

int mg_socket_close(int sock) { return MG_RETRY_ON_EINTR(close(sock)); }

char *mg_socket_error() { return strerror(errno); }
