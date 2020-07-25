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

#ifdef ON_POSIX
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif  // ON_POSIX

#ifdef ON_WINDOWS
#include <Ws2tcpip.h>
#include <winsock2.h>
#endif  // ON_WINDOWS

#include "mgsession.h"

int mg_socket_init(int af, int type, int protocol);

int mg_socket_connect(int sock, const struct sockaddr* addr, socklen_t addrlen);

int mg_socket_options(int sock, mg_session* session);

int mg_socket_send(int sock, const void* buf, int len);

int mg_socket_receive(int sock, void* buf, int len);

int mg_socket_close(int sock);

char* mg_socket_error();
