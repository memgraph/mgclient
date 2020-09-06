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

#include "mgclient.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if ON_POSIX
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif  // ON_POSIX

#if ON_WINDOWS
#include <Ws2tcpip.h>
#include <windows.h>
#include <winsock2.h>
// TODO(gitbuda): Add more https://gist.github.com/PkmX/63dd23f28ba885be53a5.
#define htobe32(x) __builtin_bswap32(x)
#define be32toh(x) __builtin_bswap32(x)
#endif  // ON_WINDOWS

#include "mgcommon.h"
#include "mgconstants.h"
#include "mgmessage.h"
#include "mgsession.h"
#include "mgsocket.h"

int mg_init() { return mg_socket_init(); }

typedef struct mg_session_params {
  const char *address;
  const char *host;
  uint16_t port;
  const char *username;
  const char *password;
  const char *client_name;
  enum mg_sslmode sslmode;
  const char *sslcert;
  const char *sslkey;
  int (*trust_callback)(const char *, const char *, const char *, const char *,
                        void *);
  void *trust_data;
} mg_session_params;

mg_session_params *mg_session_params_make() {
  mg_session_params *params =
      mg_allocator_malloc(&mg_system_allocator, sizeof(mg_session_params));
  if (!params) {
    return NULL;
  }
  params->address = NULL;
  params->host = NULL;
  params->port = 0;
  params->username = NULL;
  params->password = NULL;
  params->client_name = MG_CLIENT_NAME_DEFAULT;
  params->sslmode = MG_SSLMODE_DISABLE;
  params->sslcert = NULL;
  params->sslkey = NULL;
  params->trust_callback = NULL;
  params->trust_data = NULL;
  return params;
}

void mg_session_params_destroy(mg_session_params *params) {
  if (!params) {
    return;
  }
  mg_allocator_free(&mg_system_allocator, params);
}

void mg_session_params_set_address(mg_session_params *params,
                                   const char *address) {
  params->address = address;
}

void mg_session_params_set_host(mg_session_params *params, const char *host) {
  params->host = host;
}

void mg_session_params_set_port(mg_session_params *params, uint16_t port) {
  params->port = port;
}

void mg_session_params_set_username(mg_session_params *params,
                                    const char *username) {
  params->username = username;
}

void mg_session_params_set_password(mg_session_params *params,
                                    const char *password) {
  params->password = password;
}

void mg_session_params_set_client_name(mg_session_params *params,
                                       const char *client_name) {
  params->client_name = client_name;
}

void mg_session_params_set_sslmode(mg_session_params *params,
                                   enum mg_sslmode sslmode) {
  params->sslmode = sslmode;
}

void mg_session_params_set_sslcert(mg_session_params *params,
                                   const char *sslcert) {
  params->sslcert = sslcert;
}

void mg_session_params_set_sslkey(mg_session_params *params,
                                  const char *sslkey) {
  params->sslkey = sslkey;
}

void mg_session_params_set_trust_callback(
    mg_session_params *params,
    int (*trust_callback)(const char *, const char *, const char *,
                          const char *, void *)) {
  params->trust_callback = trust_callback;
}

void mg_session_params_set_trust_data(mg_session_params *params,
                                      void *trust_data) {
  params->trust_data = trust_data;
}

const char *mg_session_params_get_address(const mg_session_params *params) {
  return params->address;
}

const char *mg_session_params_get_host(const mg_session_params *params) {
  return params->host;
}

uint16_t mg_session_params_get_port(const mg_session_params *params) {
  return params->port;
}

const char *mg_session_params_get_username(const mg_session_params *params) {
  return params->username;
}

const char *mg_session_params_get_password(const mg_session_params *params) {
  return params->password;
}

const char *mg_session_params_get_client_name(const mg_session_params *params) {
  return params->client_name;
}

enum mg_sslmode mg_session_params_get_sslmode(const mg_session_params *params) {
  return params->sslmode;
}

const char *mg_session_params_get_sslcert(const mg_session_params *params) {
  return params->sslcert;
}

const char *mg_session_params_get_sslkey(const mg_session_params *params) {
  return params->sslkey;
}

int (*mg_session_params_get_trust_callback(const mg_session_params *params))(
    const char *, const char *, const char *, const char *, void *) {
  return params->trust_callback;
}

void *mg_session_params_get_trust_data(const mg_session_params *params) {
  return params->trust_data;
}

int validate_session_params(const mg_session_params *params,
                            mg_session *session) {
  if ((!params->address && !params->host) ||
      (params->address && params->host)) {
    mg_session_set_error(
        session,
        "exactly one of 'host' and 'address' parameters must be specified");
    return MG_ERROR_BAD_PARAMETER;
  }
  if ((params->username && !params->password) ||
      (!params->username && params->password)) {
    mg_session_set_error(session,
                         "both username and password should be provided");
    return MG_ERROR_BAD_PARAMETER;
  }
  if ((params->sslcert && !params->sslkey) ||
      (!params->sslcert && params->sslkey)) {
    mg_session_set_error(session, "both sslcert and sslkey should be provided");
    return MG_ERROR_BAD_PARAMETER;
  }

  return 0;
}

static int mg_bolt_handshake(mg_session *session) {
  const uint32_t VERSION_NONE = htobe32(0);
  const uint32_t VERSION_1 = htobe32(1);
  if (mg_transport_send(session->transport, MG_HANDSHAKE_MAGIC,
                        strlen(MG_HANDSHAKE_MAGIC)) != 0 ||
      mg_transport_send(session->transport, (char *)&VERSION_1, 4) != 0 ||
      mg_transport_send(session->transport, (char *)&VERSION_NONE, 4) != 0 ||
      mg_transport_send(session->transport, (char *)&VERSION_NONE, 4) != 0 ||
      mg_transport_send(session->transport, (char *)&VERSION_NONE, 4) != 0) {
    mg_session_set_error(session, "failed to send handshake data");
    return MG_ERROR_SEND_FAILED;
  }
  uint32_t server_version;
  if (mg_transport_recv(session->transport, (char *)&server_version, 4) != 0) {
    mg_session_set_error(session, "failed to receive handshake response");
    return MG_ERROR_RECV_FAILED;
  }
  if (server_version != VERSION_1) {
    mg_session_set_error(session, "unsupported protocol version: %" PRIu32,
                         be32toh(server_version));
    return MG_ERROR_PROTOCOL_VIOLATION;
  }
  return 0;
}

static mg_map *build_auth_token(const char *username, const char *password) {
  mg_map *auth_token = mg_map_make_empty(3);
  if (!auth_token) {
    return NULL;
  }

  assert((username && password) || (!username && !password));
  if (username) {
    mg_value *scheme = mg_value_make_string("basic");
    if (!scheme || mg_map_insert_unsafe(auth_token, "scheme", scheme) != 0) {
      goto cleanup;
    }

    mg_value *principal = mg_value_make_string(username);
    if (!principal ||
        mg_map_insert_unsafe(auth_token, "principal", principal)) {
      goto cleanup;
    }

    mg_value *credentials = mg_value_make_string(password);
    if (!credentials ||
        mg_map_insert_unsafe(auth_token, "credentials", credentials)) {
      goto cleanup;
    }
  } else {
    mg_value *scheme = mg_value_make_string("none");
    if (!scheme || mg_map_insert_unsafe(auth_token, "scheme", scheme) != 0) {
      goto cleanup;
    }
  }

  return auth_token;

cleanup:
  mg_map_destroy(auth_token);
  return NULL;
}

int handle_failure_message(mg_session *session, mg_message_failure *message) {
  int type = MG_ERROR_UNKNOWN_ERROR;
  const mg_string *code = NULL;
  const mg_string *error_message = NULL;

  {
    const mg_value *tmp = mg_map_at(message->metadata, "code");
    if (tmp && mg_value_get_type(tmp) == MG_VALUE_TYPE_STRING) {
      code = mg_value_string(tmp);
    }
    tmp = mg_map_at(message->metadata, "message");
    if (tmp && mg_value_get_type(tmp) == MG_VALUE_TYPE_STRING) {
      error_message = mg_value_string(tmp);
    }
  }

  if (!code) {
    goto done;
  }

  char *type_begin = strchr(code->data, '.');
  if (!type_begin) {
    goto done;
  }
  type_begin++;
  char *type_end = strchr(type_begin, '.');
  if (!type_end) {
    goto done;
  }
  size_t type_size = (size_t)(type_end - type_begin);

  if (strncmp(type_begin, "ClientError", type_size) == 0) {
    type = MG_ERROR_CLIENT_ERROR;
  }
  if (strncmp(type_begin, "TransientError", type_size) == 0) {
    type = MG_ERROR_TRANSIENT_ERROR;
  }
  if (strncmp(type_begin, "DatabaseError", type_size) == 0) {
    type = MG_ERROR_DATABASE_ERROR;
  }

done:
  if (error_message) {
    mg_session_set_error(session, "%.*s", error_message->size,
                         error_message->data);
  } else {
    mg_session_set_error(session, "unknown error occurred");
  }
  return type;
}

static int mg_bolt_init(mg_session *session, const mg_session_params *params) {
  mg_map *auth_token = build_auth_token(params->username, params->password);
  if (!auth_token) {
    return MG_ERROR_OOM;
  }

  int status =
      mg_session_send_init_message(session, params->client_name, auth_token);
  mg_map_destroy(auth_token);

  if (status != 0) {
    return status;
  }

  MG_RETURN_IF_FAILED(mg_session_receive_message(session));

  mg_message *response;
  MG_RETURN_IF_FAILED(mg_session_read_bolt_message(session, &response));

  if (response->type == MG_MESSAGE_TYPE_SUCCESS) {
    status = 0;
  } else if (response->type == MG_MESSAGE_TYPE_FAILURE) {
    status = handle_failure_message(session, response->failure_v);
  } else {
    status = MG_ERROR_PROTOCOL_VIOLATION;
    mg_session_set_error(session, "unexpected message type");
  }

  mg_message_destroy_ca(response, session->decoder_allocator);
  return status;
}

static int init_tcp_connection(const mg_session_params *params, int *sockfd,
                               struct sockaddr *peer_addr,
                               mg_session *session) {
  struct addrinfo hint;
  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_UNSPEC;
  hint.ai_socktype = SOCK_STREAM;
  struct addrinfo *addr_list;

  char portstr[6];
  sprintf(portstr, "%" PRIu16, params->port);

  int getaddrinfo_status;
  if (params->host) {
    getaddrinfo_status = getaddrinfo(params->host, portstr, &hint, &addr_list);
  } else if (params->address) {
    hint.ai_flags = AI_NUMERICHOST;
    getaddrinfo_status =
        getaddrinfo(params->address, portstr, &hint, &addr_list);
  } else {
    abort();
  }

  if (getaddrinfo_status != 0) {
    mg_session_set_error(session, "getaddrinfo failed: %s",
                         gai_strerror(getaddrinfo_status));
    return MG_ERROR_NETWORK_FAILURE;
  }

#ifdef ON_POSIX
  int tsockfd = -1;
  int status = 0;
#endif  // ON_POSIX

#ifdef ON_WINDOWS
  SOCKET tsockfd = INVALID_SOCKET;
  int status = 0;
#endif  // ON_WINDOWS

  for (struct addrinfo *curr_addr = addr_list; curr_addr;
       curr_addr = curr_addr->ai_next) {
    // TODO(gitbuda): Cross-platform error handling.
    tsockfd = mg_socket_create(curr_addr->ai_family, curr_addr->ai_socktype,
                               curr_addr->ai_protocol);
    if (tsockfd == -1) {
      status = MG_ERROR_NETWORK_FAILURE;
      mg_session_set_error(session, "couldn't open socket: %s",
                           strerror(errno));
      continue;
    }
    // TODO(gitbuda): Cross-platform error handling.
    if (mg_socket_connect(tsockfd, curr_addr->ai_addr, curr_addr->ai_addrlen) !=
        0) {
      status = MG_ERROR_NETWORK_FAILURE;
      mg_session_set_error(session, "couldn't connect to host: %s",
                           strerror(errno));
      // TODO(gitbuda): Cross-platform error handling.
      if (MG_RETRY_ON_EINTR(mg_socket_close(tsockfd)) != 0) {
        abort();
      }
      tsockfd = -1;
    } else {
      memcpy(peer_addr, curr_addr->ai_addr, sizeof(struct sockaddr));
      break;
    }
  }

  freeaddrinfo(addr_list);

  if (tsockfd == -1) {
    assert(status != 0);
    return status;
  }

  int set_options_status = mg_socket_options(tsockfd, session);
  if (set_options_status == MG_ERROR_NETWORK_FAILURE) {
    return MG_ERROR_NETWORK_FAILURE;
  }

  *sockfd = tsockfd;
  return 0;
}

static int get_hostname_and_ip(const struct sockaddr *peer_addr, char *hostname,
                               char *ip, mg_session *session) {
  switch (peer_addr->sa_family) {
    case AF_INET:
      if (!inet_ntop(AF_INET, &((struct sockaddr_in *)peer_addr)->sin_addr, ip,
                     INET6_ADDRSTRLEN)) {
        mg_session_set_error(session, "failed to get server IP: %s",
                             strerror(errno));
        return MG_ERROR_NETWORK_FAILURE;
      }
      break;

    case AF_INET6:
      if (!inet_ntop(AF_INET6, &((struct sockaddr_in6 *)peer_addr)->sin6_addr,
                     ip, INET6_ADDRSTRLEN)) {
        mg_session_set_error(session, "failed to get server IP: %s",
                             strerror(errno));
        return MG_ERROR_NETWORK_FAILURE;
      }
      break;

    default:
      // Should not happen with addresses returned from getaddrinfo.
      abort();
  }

  int status = getnameinfo(peer_addr, sizeof(struct sockaddr), hostname,
                           NI_MAXHOST, NULL, 0, 0);
  if (status != 0) {
    mg_session_set_error(session, "failed to get server name: %s",
                         gai_strerror(status));
    return MG_ERROR_NETWORK_FAILURE;
  }
  return 0;
}

int mg_connect_ca(const mg_session_params *params, mg_session **session,
                  mg_allocator *allocator) {
  mg_session *tsession = mg_session_init(allocator);
  if (!tsession) {
    return MG_ERROR_OOM;
  }

  int status = 0;
  int sockfd = -1;

  status = validate_session_params(params, tsession);
  if (status != 0) {
    goto cleanup;
  }

  struct sockaddr peer_addr;
  status = init_tcp_connection(params, &sockfd, &peer_addr, tsession);
  if (status != 0) {
    goto cleanup;
  }

  char ip[INET6_ADDRSTRLEN];
  char hostname[NI_MAXHOST];
  status = get_hostname_and_ip(&peer_addr, hostname, ip, tsession);
  if (status != 0) {
    goto cleanup;
  }

  switch (params->sslmode) {
    case MG_SSLMODE_DISABLE:
      status = mg_raw_transport_init(
          sockfd, (mg_raw_transport **)&tsession->transport, allocator);
      if (status != 0) {
        mg_session_set_error(tsession, "failed to initialize connection");
        goto cleanup;
      }
      break;
    case MG_SSLMODE_REQUIRE: {
      mg_secure_transport *ttransport;
      status = mg_secure_transport_init(sockfd, params->sslcert, params->sslkey,
                                        &ttransport, allocator);
      if (status != 0) {
        mg_session_set_error(tsession,
                             "failed to initialize secure connection");
        goto cleanup;
      }
      tsession->transport = (mg_transport *)ttransport;
      if (params->trust_callback) {
        int trust_result = params->trust_callback(
            hostname, ip, ttransport->peer_pubkey_type,
            ttransport->peer_pubkey_fp, params->trust_data);
        if (trust_result != 0) {
          mg_session_set_error(tsession,
                               "trust callback returned non-zero value");
          status = MG_ERROR_TRUST_CALLBACK;
          goto cleanup;
        }
      }
      break;
    }
    default:
      // Should not get here.
      abort();
  }

  // mg_transport object took ownership of the socket.
  sockfd = -1;

  status = mg_bolt_handshake(tsession);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_bolt_init(tsession, params);
  if (status != 0) {
    goto cleanup;
  }

  tsession->status = MG_SESSION_READY;
  *session = tsession;
  return 0;

cleanup:
  // TODO(gitbuda): Cross-platform error handling.
  if (sockfd >= 0 && MG_RETRY_ON_EINTR(mg_socket_close(sockfd)) != 0) {
    abort();
  }
  *session = tsession;
  mg_session_invalidate(tsession);
  return status;
}

int mg_connect(const mg_session_params *params, mg_session **session) {
  return mg_connect_ca(params, session, &mg_system_allocator);
}

int mg_session_run(mg_session *session, const char *query, const mg_map *params,
                   const mg_list **columns) {
  if (session->status == MG_SESSION_BAD) {
    mg_session_set_error(session, "bad session");
    return MG_ERROR_BAD_CALL;
  }
  if (session->status == MG_SESSION_EXECUTING) {
    mg_session_set_error(session, "already executing a query");
    return MG_ERROR_BAD_CALL;
  }
  assert(session->status == MG_SESSION_READY);

  mg_message_destroy_ca(session->result.message, session->decoder_allocator);
  session->result.message = NULL;
  mg_list_destroy_ca(session->result.columns, session->allocator);
  session->result.columns = NULL;

  if (!params) {
    params = &mg_empty_map;
  }

  int status = 0;
  status = mg_session_send_run_message(session, query, params);
  if (status != 0) {
    goto fatal_failure;
  }

  status = mg_session_receive_message(session);
  if (status != 0) {
    goto fatal_failure;
  }

  mg_message *response;

  status = mg_session_read_bolt_message(session, &response);
  if (status != 0) {
    goto fatal_failure;
  }

  if (response->type == MG_MESSAGE_TYPE_SUCCESS) {
    const mg_value *columns_tmp =
        mg_map_at(response->success_v->metadata, "fields");
    if (!columns_tmp || mg_value_get_type(columns_tmp) != MG_VALUE_TYPE_LIST) {
      status = MG_ERROR_PROTOCOL_VIOLATION;
      mg_message_destroy_ca(response, session->decoder_allocator);
      mg_session_set_error(session, "invalid response metadata");
      goto fatal_failure;
    }
    session->result.columns =
        mg_list_copy_ca(mg_value_list(columns_tmp), session->allocator);
    mg_message_destroy_ca(response, session->decoder_allocator);
    if (!session->result.columns) {
      mg_session_set_error(session, "out of memory");
      return MG_ERROR_OOM;
    }
    status = mg_session_send_pull_all_message(session);
    if (status != 0) {
      goto fatal_failure;
    }
    if (columns) {
      *columns = session->result.columns;
    }
    session->status = MG_SESSION_EXECUTING;
    return 0;
  }

  if (response->type == MG_MESSAGE_TYPE_FAILURE) {
    int failure_status = handle_failure_message(session, response->failure_v);
    mg_message_destroy_ca(response, session->decoder_allocator);

    status = mg_session_send_ack_failure_message(session);
    if (status != 0) {
      goto fatal_failure;
    }

    status = mg_session_receive_message(session);
    if (status != 0) {
      goto fatal_failure;
    }

    status = mg_session_read_bolt_message(session, &response);
    if (status != 0) {
      goto fatal_failure;
    }

    if (response->type != MG_MESSAGE_TYPE_SUCCESS) {
      mg_message_destroy_ca(response, session->decoder_allocator);
      status = MG_ERROR_PROTOCOL_VIOLATION;
      mg_session_set_error(session, "unexpected message type");
      goto fatal_failure;
    }

    mg_message_destroy_ca(response, session->decoder_allocator);
    return failure_status;
  }

  status = MG_ERROR_PROTOCOL_VIOLATION;
  mg_message_destroy_ca(response, session->decoder_allocator);
  mg_session_set_error(session, "unexpected message type");

fatal_failure:
  mg_session_invalidate(session);
  assert(status != 0);
  return status;
}

int mg_session_pull(mg_session *session, mg_result **result) {
  if (session->status == MG_SESSION_BAD) {
    mg_session_set_error(session, "bad session");
    return MG_ERROR_BAD_CALL;
  }
  if (session->status == MG_SESSION_READY) {
    mg_session_set_error(session, "not executing a query");
    return MG_ERROR_BAD_CALL;
  }
  assert(session->status == MG_SESSION_EXECUTING);

  mg_message_destroy_ca(session->result.message, session->decoder_allocator);
  session->result.message = NULL;

  int status = 0;

  mg_message *message = NULL;
  status = mg_session_receive_message(session);
  if (status != 0) {
    goto fatal_failure;
  }

  status = mg_session_read_bolt_message(session, &message);
  if (status != 0) {
    goto fatal_failure;
  }

  if (message->type == MG_MESSAGE_TYPE_RECORD) {
    session->result.message = message;
    *result = &session->result;
    return 1;
  }

  if (message->type == MG_MESSAGE_TYPE_SUCCESS) {
    session->result.message = message;
    *result = &session->result;
    session->status = MG_SESSION_READY;
    return 0;
  }

  if (message->type == MG_MESSAGE_TYPE_FAILURE) {
    int failure_status = handle_failure_message(session, message->failure_v);
    mg_message_destroy_ca(message, session->decoder_allocator);

    status = mg_session_send_ack_failure_message(session);
    if (status != 0) {
      goto fatal_failure;
    }

    status = mg_session_receive_message(session);
    if (status != 0) {
      goto fatal_failure;
    }

    status = mg_session_read_bolt_message(session, &message);
    if (status != 0) {
      goto fatal_failure;
    }

    if (message->type != MG_MESSAGE_TYPE_SUCCESS) {
      mg_message_destroy_ca(message, session->decoder_allocator);
      status = MG_ERROR_PROTOCOL_VIOLATION;
      mg_session_set_error(session, "unexpected message type");
      goto fatal_failure;
    }

    mg_message_destroy_ca(message, session->decoder_allocator);
    session->status = MG_SESSION_READY;
    return failure_status;
  }

  status = MG_ERROR_PROTOCOL_VIOLATION;
  mg_session_set_error(session, "unexpected message type");
  mg_message_destroy_ca(message, session->decoder_allocator);

fatal_failure:
  mg_session_invalidate(session);
  return status;
}

const mg_list *mg_result_columns(const mg_result *result) {
  return result->columns;
}

const mg_list *mg_result_row(const mg_result *result) {
  if (result->message->type != MG_MESSAGE_TYPE_RECORD) {
    return NULL;
  }
  return result->message->record_v->fields;
}

const mg_map *mg_result_summary(const mg_result *result) {
  if (result->message->type != MG_MESSAGE_TYPE_SUCCESS) {
    return NULL;
  }
  return result->message->success_v->metadata;
}
