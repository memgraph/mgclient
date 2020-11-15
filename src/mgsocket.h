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

#ifdef MGCLIENT_ON_LINUX
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif  // MGCLIENT_ON_LINUX

#ifdef MGCLIENT_ON_WINDOWS
#include <Ws2tcpip.h>
#include <windows.h>
#include <winsock2.h>
#endif  // MGCLIENT_ON_WINDOWS

#include "mgclient.h"
#include "mgsession.h"

/// Initializes underlying resources. Has to be called at the beginning of a
/// process using socket resources.
int mg_socket_init();

/// Returns a descriptor referencing the new socket or MG_ERROR_SOCKET in the
/// case of any failure.
int mg_socket_create(int af, int type, int protocol);

/// Checks for errors after \ref mg_socket_create call.
///
/// \param[in]  sock    Return value out of \ref mg_socket_create call.
/// \param[in]  session A pointer to the session object to set the error
///                     message if required.
///
/// \return \ref MG_ERROR in case of an error or \ref MG_SUCCESS if there is no
/// error. In the error case, session will have the underlying error message
/// set.
int mg_socket_create_handle_error(int sock, mg_session *session);

int mg_socket_connect(int sock, const struct sockaddr *addr, socklen_t addrlen);

int mg_socket_connect_handle_error(int *sock, int status, mg_session *session);

int mg_socket_options(int sock, mg_session *session);

ssize_t mg_socket_send(int sock, const void *buf, int len);

ssize_t mg_socket_receive(int sock, void *buf, int len);

int mg_socket_poll(struct pollfd *fds, unsigned int nfds, int timeout);

int mg_socket_pair(int d, int type, int protocol, int *sv);

int mg_socket_close(int sock);

/// Used to get a native error message after some socket call fails.
/// Has to be called immediately after the failed socket function.
char *mg_socket_error();

/// Should be called at the end of any process which previously called the
/// \ref mg_socket_init function.
void mg_socket_finalize();
