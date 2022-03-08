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

#ifndef MGCLIENT_MGSOCKET_H
#define MGCLIENT_MGSOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MGCLIENT_ON_APPLE
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif  // MGCLIENT_ON_APPLE

#ifdef MGCLIENT_ON_LINUX
#include <arpa/inet.h>
#include <errno.h>
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
#ifdef _MSC_VER
typedef long ssize_t;
#endif
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

/// Connects the socket referred to by the sock descriptor to the address
/// specified by addr. The addrlen argument specifies the size of addr.
///
/// \return \ref MG_ERROR in case of an error or \ref MG_SUCCESS if there is no
/// error.
int mg_socket_connect(int sock, const struct sockaddr *addr, socklen_t addrlen);

/// Checks for errors after \ref mg_socket_connect call.
///
/// \param[out] sock    Return value out of \ref mg_socket_create call.
/// \param[in]  status  Return value out of \ref mg_socket_connect call.
/// \param[in]  session A pointer to the session object to set the error
///                     message if required.
///
/// \return \ref MG_ERROR in case of an error or \ref MG_SUCCESS if there is no
/// error. In the error case, session will have the underlying error message
/// set + value referenced by the sock will be set to MG_ERROR_SOCKET.
int mg_socket_connect_handle_error(int *sock, int status, mg_session *session);

/// Sets options for a socket referenced with the given sock descriptor.
int mg_socket_options(int sock, mg_session *session);

/// Sends len bytes from buf to the socket referenced by the sock descriptor.
ssize_t mg_socket_send(int sock, const void *buf, int len);

/// Reads len bytes to buf from the socket referenced by the sock descriptor.
ssize_t mg_socket_receive(int sock, void *buf, int len);

/// Waits for one of a set of file descriptors to become ready to perform I/O.
int mg_socket_poll(struct pollfd *fds, unsigned int nfds, int timeout);

/// Creates a socket pair.
int mg_socket_pair(int d, int type, int protocol, int *sv);

/// Closes the socket referenced by the sock descriptor.
int mg_socket_close(int sock);

/// Used to get a native error message after some socket call fails.
/// Has to be called immediately after the failed socket function.
char *mg_socket_error();

/// Should be called at the end of any process which previously called the
/// \ref mg_socket_init function.
void mg_socket_finalize();

#ifdef __cplusplus
}
#endif

#endif /* MGCLIENT_MGSOCKET_H */
