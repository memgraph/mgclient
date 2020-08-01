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

#include "mgclient-error.h"

int mg_socket_init() {
  // TODO(gitbuda): Handle errors, return MG_ERRORs.
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data);
}

int mg_socket_create(int af, int type, int protocol) {
  return socket(af, type, protocol);
}

int mg_socket_connect(int sock, const struct sockaddr* addr,
                      socklen_t addrlen) {
  return connect(sock, addr, addrlen);
}

int mg_socket_options(int sock, mg_session* session) {
  return MG_ERROR_UNIMPLEMENTED;
}

int mg_socket_send(int sock, const void* buf, int len) {
  return send(sock, buf, len, 0);
}

int mg_socket_receive(int sock, void* buf, int len) {
  return recv(sock, buf, len, 0);
}

int mg_socket_close(int sock) { return closesocket(sock); }

char* mg_socket_error() {
  // TODO(gitbuda): Implement mg_socket_error for Windows.
  return "";
}
