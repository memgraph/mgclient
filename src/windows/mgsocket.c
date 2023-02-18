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

#include <limits.h>

// Please refer to https://docs.microsoft.com/en-us/windows/win32/api/winsock2/
// for more details about Windows system calls.

int mg_socket_init() {
  WSADATA data;
  int status = WSAStartup(MAKEWORD(2, 2), &data);
  if (status != 0) {
    fprintf(stderr, "WSAStartup failed: %s\n", mg_socket_error());
    abort();
  }
  return MG_SUCCESS;
}

int mg_socket_create(int af, int type, int protocol) {
  SOCKET sock = socket(af, type, protocol);
  // Useful info here https://stackoverflow.com/questions/10817252.
  if (sock == INVALID_SOCKET) {
    return MG_ERROR_SOCKET;
  }
  if (sock > INT_MAX) {
    fprintf(stderr,
            "Implementation is wrong. Unsigned result of socket system call "
            "can not be stored to signed data type. Please contact the"
            "maintainer.\n");
    abort();
  }
  return (int)sock;
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
  int status = connect(sock, addr, addrlen);
  if (status != 0) {
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
  int sent = send(sock, buf, len, 0);
  if (sent == SOCKET_ERROR) {
    return -1;
  }
  return sent;
}

ssize_t mg_socket_receive(int sock, void *buf, int len) {
  int received = recv(sock, buf, len, 0);
  if (received == SOCKET_ERROR) {
    return -1;
  }
  return received;
}

int mg_socket_poll(struct pollfd *fds, unsigned int nfds, int timeout) {
  return WSAPoll(fds, nfds, timeout);
}

// Implementation here
// https://nlnetlabs.nl/svn/unbound/tags/release-1.0.1/compat/socketpair.c
// does not work.
int mg_socket_pair(int d, int type, int protocol, int *sv) {
  (void)d;
  (void)type;
  (void)protocol;
  (void)sv;
  return MG_ERROR_UNIMPLEMENTED;
}

int mg_socket_close(int sock) {
  int shutdown_status = shutdown(sock, SD_BOTH);
  if (shutdown_status != 0) {
    fprintf(stderr, "Fail to shutdown socket: %s\n", mg_socket_error());
  }
  int closesocket_status = closesocket(sock);
  if (closesocket_status != 0) {
    fprintf(stderr, "Fail to close socket: %s\n", mg_socket_error());
  }
  return MG_SUCCESS;
}

char *mg_socket_error() {
  // FormatMessage could be used but a caller would have
  // to take care of the allocated memory (LocalFree call).
  switch (WSAGetLastError()) {
    case WSA_INVALID_HANDLE:
      return "Specified event object handle is invalid.";
    case WSA_NOT_ENOUGH_MEMORY:
      return "Insufficient memory available.";
    case WSA_INVALID_PARAMETER:
      return "One or more parameters are invalid.";
    case WSA_OPERATION_ABORTED:
      return "Overlapped operation aborted.";
    case WSA_IO_INCOMPLETE:
      return "Overlapped I/O event object not in signaled state.";
    case WSA_IO_PENDING:
      return "Overlapped operations will complete later.";
    case WSAEINTR:
      return "Interrupted function call.";
    case WSAEBADF:
      return "File handle is not valid.";
    case WSAEACCES:
      return "Permission denied.";
    case WSAEFAULT:
      return "Bad address.";
    case WSAEINVAL:
      return "Invalid argument.";
    case WSAEMFILE:
      return "Too many open files.";
    case WSAEWOULDBLOCK:
      return "Resource temporarily unavailable.";
    case WSAEINPROGRESS:
      return "Operation now in progress.";
    case WSAEALREADY:
      return "Operation already in progress.";
    case WSAENOTSOCK:
      return "Socket operation on nonsocket.";
    case WSAEDESTADDRREQ:
      return "Destination address required.";
    case WSAEMSGSIZE:
      return "Message too long.";
    case WSAEPROTOTYPE:
      return "Protocol wrong type for socket.";
    case WSAENOPROTOOPT:
      return "Bad protocol option.";
    case WSAEPROTONOSUPPORT:
      return "Protocol not supported.";
    case WSAESOCKTNOSUPPORT:
      return "Socket type not supported.";
    case WSAEOPNOTSUPP:
      return "Operation not supported.";
    case WSAEPFNOSUPPORT:
      return "Protocol family not supported.";
    case WSAEAFNOSUPPORT:
      return "Address family not supported by protocol family.";
    case WSAEADDRINUSE:
      return "Address already in use.";
    case WSAEADDRNOTAVAIL:
      return "Cannot assign requested address.";
    case WSAENETDOWN:
      return "Network is down.";
    case WSAENETUNREACH:
      return "Network is unreachable.";
    case WSAENETRESET:
      return "Network dropped connection on reset.";
    case WSAECONNABORTED:
      return "Software caused connection abort.";
    case WSAECONNRESET:
      return "Connection reset by peer.";
    case WSAENOBUFS:
      return "No buffer space available.";
    case WSAEISCONN:
      return "Socket is already connected.";
    case WSAENOTCONN:
      return "Socket is not connected.";
    case WSAESHUTDOWN:
      return "Cannot send after socket shutdown.";
    case WSAETOOMANYREFS:
      return "Too many references to some kernel object.";
    case WSAETIMEDOUT:
      return "Connection timed out.";
    case WSAECONNREFUSED:
      return "Connection refused.";
    case WSAELOOP:
      return "Cannot translate name.";
    case WSAENAMETOOLONG:
      return "Name too long.";
    case WSAEHOSTDOWN:
      return "Host is down.";
    case WSAEHOSTUNREACH:
      return "No route to host.";
    case WSAENOTEMPTY:
      return "Directory not empty.";
    case WSAEPROCLIM:
      return "Too many processes.";
    case WSAEUSERS:
      return "User quota exceeded.";
    case WSAEDQUOT:
      return "Disk quota exceeded.";
    case WSAESTALE:
      return "Stale file handle reference.";
    case WSAEREMOTE:
      return "Item is remote.";
    case WSASYSNOTREADY:
      return "Network subsystem is unavailable.";
    case WSAVERNOTSUPPORTED:
      return "Winsock.dll version out of range.";
    case WSANOTINITIALISED:
      return "Successful WSAStartup not yet performed.";
    case WSAEDISCON:
      return "Graceful shutdown in progress.";
    case WSAENOMORE:
      return "No more results.";
    case WSAECANCELLED:
      return "Call has been canceled.";
    case WSAEINVALIDPROCTABLE:
      return "Procedure call table is invalid.";
    case WSAEINVALIDPROVIDER:
      return "Service provider is invalid.";
    case WSAEPROVIDERFAILEDINIT:
      return "Service provider failed to initialize.";
    case WSASYSCALLFAILURE:
      return "System call failure.";
    case WSASERVICE_NOT_FOUND:
      return "Service not found.";
    case WSATYPE_NOT_FOUND:
      return "Class type not found.";
    case WSA_E_NO_MORE:
      return "No more results.";
    case WSA_E_CANCELLED:
      return "Call was canceled.";
    case WSAEREFUSED:
      return "Database query was refused.";
    case WSAHOST_NOT_FOUND:
      return "Host not found.";
    case WSATRY_AGAIN:
      return "Nonauthoritative host not found.";
    case WSANO_RECOVERY:
      return "This is a nonrecoverable error.";
    case WSANO_DATA:
      return "Valid name, no data record of requested type.";
    case WSA_QOS_RECEIVERS:
      return "At least one QoS reserve has arrived.";
    case WSA_QOS_SENDERS:
      return "At least one QoS send path has arrived.";
    case WSA_QOS_NO_SENDERS:
      return "There are no QoS senders.";
    case WSA_QOS_NO_RECEIVERS:
      return "There are no QoS receivers.";
    case WSA_QOS_REQUEST_CONFIRMED:
      return "The QoS reserve request has been confirmed.";
    case WSA_QOS_ADMISSION_FAILURE:
      return "QoS admission error.";
    case WSA_QOS_POLICY_FAILURE:
      return "QoS policy failure.";
    case WSA_QOS_BAD_STYLE:
      return "QoS bad style.";
    case WSA_QOS_BAD_OBJECT:
      return "QoS bad object.";
    case WSA_QOS_TRAFFIC_CTRL_ERROR:
      return "QoS traffic control error.";
    case WSA_QOS_GENERIC_ERROR:
      return "QoS generic error.";
    case WSA_QOS_ESERVICETYPE:
      return "QoS service type error.";
    case WSA_QOS_EFLOWSPEC:
      return "QoS flowspec error.";
    case WSA_QOS_EPROVSPECBUF:
      return "Invalid QoS provider buffer.";
    case WSA_QOS_EFILTERSTYLE:
      return "Invalid QoS filter style.";
    case WSA_QOS_EFILTERTYPE:
      return "Invalid QoS filter type.";
    case WSA_QOS_EFILTERCOUNT:
      return "Incorrect QoS filter count.";
    case WSA_QOS_EOBJLENGTH:
      return "Invalid QoS object length.";
    case WSA_QOS_EFLOWCOUNT:
      return "Incorrect QoS flow count.";
    case WSA_QOS_EUNKOWNPSOBJ:
      return "Unrecognized QoS object.";
    case WSA_QOS_EPOLICYOBJ:
      return "Invalid QoS policy object.";
    case WSA_QOS_EFLOWDESC:
      return "Invalid QoS flow descriptor.";
    case WSA_QOS_EPSFLOWSPEC:
      return "Invalid QoS provider-specific flowspec.";
    case WSA_QOS_EPSFILTERSPEC:
      return "Invalid QoS provider-specific filterspec.";
    case WSA_QOS_ESDMODEOBJ:
      return "Invalid QoS shape discard mode object.";
    case WSA_QOS_ESHAPERATEOBJ:
      return "Invalid QoS shaping rate object.";
    case WSA_QOS_RESERVED_PETYPE:
      return "Reserved policy QoS element type.";
    default:
      return "Unknown WSA error.";
  }
  return "Unknown WSA error.";
}

void mg_socket_finalize() {
  if (WSACleanup() != 0) {
    fprintf(stderr, "WSACleanup failed: %s\n", mg_socket_error());
  }
}
