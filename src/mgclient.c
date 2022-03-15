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

#include "mgcommon.h"
#include "mgconstants.h"
#include "mgmessage.h"
#include "mgsession.h"
#include "mgsocket.h"
#include "mgtransport.h"
#include "mgvalue.h"

const char *mg_client_version() { return MGCLIENT_VERSION; }

int mg_init() { return mg_socket_init(); }

void mg_finalize() { mg_socket_finalize(); }

typedef struct mg_session_params {
  const char *address;
  const char *host;
  uint16_t port;
  const char *username;
  const char *password;
  const char *user_agent;
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
  params->user_agent = MG_USER_AGENT;
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

void mg_session_params_set_user_agent(mg_session_params *params,
                                      const char *user_agent) {
  params->user_agent = user_agent;
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

const char *mg_session_params_get_user_agent(const mg_session_params *params) {
  return params->user_agent;
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
  const uint32_t VERSION_4_1 = htobe32(0x0104);
  mg_transport_suspend_until_ready_to_write(session->transport);
  if (mg_transport_send(session->transport, MG_HANDSHAKE_MAGIC,
                        strlen(MG_HANDSHAKE_MAGIC)) != 0 ||
      mg_transport_send(session->transport, (char *)&VERSION_4_1, 4) != 0 ||
      mg_transport_send(session->transport, (char *)&VERSION_1, 4) != 0 ||
      mg_transport_send(session->transport, (char *)&VERSION_NONE, 4) != 0 ||
      mg_transport_send(session->transport, (char *)&VERSION_NONE, 4) != 0) {
    mg_session_set_error(session, "failed to send handshake data");
    return MG_ERROR_SEND_FAILED;
  }

  uint32_t server_version;
  mg_transport_suspend_until_ready_to_read(session->transport);
  if (mg_transport_recv(session->transport, (char *)&server_version, 4) != 0) {
    mg_session_set_error(session, "failed to receive handshake response");
    return MG_ERROR_RECV_FAILED;
  }
  if (server_version == VERSION_1) {
    session->version = 1;
  } else if (server_version == VERSION_4_1) {
    session->version = 4;
  } else {
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

int mg_bolt_init_v1(mg_session *session, const mg_session_params *params) {
  mg_map *auth_token = build_auth_token(params->username, params->password);
  if (!auth_token) {
    return MG_ERROR_OOM;
  }

  int status =
      mg_session_send_init_message(session, params->user_agent, auth_token);
  mg_map_destroy(auth_token);

  return status;
}

static mg_map *build_hello_extra(const char *user_agent, const char *username,
                                 const char *password) {
  mg_map *extra = mg_map_make_empty(4);
  if (!extra) {
    return NULL;
  }

  if (user_agent) {
    mg_value *user_agent_value = mg_value_make_string(user_agent);
    if (!user_agent_value ||
        mg_map_insert_unsafe(extra, "user_agent", user_agent_value) != 0) {
      goto cleanup;
    }
  }

  assert((username && password) || (!username && !password));
  if (username) {
    mg_value *scheme = mg_value_make_string("basic");
    if (!scheme || mg_map_insert_unsafe(extra, "scheme", scheme) != 0) {
      goto cleanup;
    }

    mg_value *principal = mg_value_make_string(username);
    if (!principal || mg_map_insert_unsafe(extra, "principal", principal)) {
      goto cleanup;
    }

    mg_value *credentials = mg_value_make_string(password);
    if (!credentials ||
        mg_map_insert_unsafe(extra, "credentials", credentials)) {
      goto cleanup;
    }
  } else {
    mg_value *scheme = mg_value_make_string("none");
    if (!scheme || mg_map_insert_unsafe(extra, "scheme", scheme) != 0) {
      goto cleanup;
    }
  }

  return extra;

cleanup:
  mg_map_destroy(extra);
  return NULL;
}

int mg_bolt_init_v4(mg_session *session, const mg_session_params *params) {
  mg_map *extra =
      build_hello_extra(params->user_agent, params->username, params->password);
  if (!extra) {
    return MG_ERROR_OOM;
  }

  int status = mg_session_send_hello_message(session, extra);
  mg_map_destroy(extra);

  return status;
}

static int mg_bolt_init(mg_session *session, const mg_session_params *params) {
  int status = session->version == 1 ? mg_bolt_init_v1(session, params)
                                     : mg_bolt_init_v4(session, params);
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
  struct addrinfo *addr_list = NULL;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  char portstr[6];
  sprintf(portstr, "%" PRIu16, params->port);
  int getaddrinfo_status;
  if (params->host) {
    getaddrinfo_status = getaddrinfo(params->host, portstr, &hints, &addr_list);
  } else if (params->address) {
    hints.ai_flags = AI_NUMERICHOST;
    getaddrinfo_status =
        getaddrinfo(params->address, portstr, &hints, &addr_list);
  } else {
    abort();
  }
  if (getaddrinfo_status != 0) {
#ifdef __EMSCRIPTEN__
    mg_session_set_error(session, "getaddrinfo failed: %d", getaddrinfo_status);
    // Not supported by emscripten:
    // gai_strerror(getaddrinfo_status));
#else
    mg_session_set_error(session, "getaddrinfo failed: %s",
                         gai_strerror(getaddrinfo_status));
#endif
    return MG_ERROR_NETWORK_FAILURE;
  }

  int tsockfd = MG_ERROR_SOCKET;
  int status = MG_SUCCESS;
  for (struct addrinfo *curr_addr = addr_list; curr_addr;
       curr_addr = curr_addr->ai_next) {
    tsockfd = mg_socket_create(curr_addr->ai_family, curr_addr->ai_socktype,
                               curr_addr->ai_protocol);
    status = mg_socket_create_handle_error(tsockfd, session);
    if (status != MG_SUCCESS) {
      continue;
    }
    status =
        mg_socket_connect(tsockfd, curr_addr->ai_addr, curr_addr->ai_addrlen);
    status = mg_socket_connect_handle_error(&tsockfd, status, session);
    if (status == MG_SUCCESS) {
      memcpy(peer_addr, curr_addr->ai_addr, sizeof(struct sockaddr));
      break;
    }
  }
  freeaddrinfo(addr_list);
  if (tsockfd == MG_ERROR_SOCKET) {
    assert(status != MG_SUCCESS);
    return status;
  }

  int set_options_status = mg_socket_options(tsockfd, session);
  if (set_options_status != MG_SUCCESS) {
    return set_options_status;
  }

  *sockfd = tsockfd;
  return 0;
}

#ifndef __EMSCRIPTEN__
static int get_hostname_and_ip(const struct sockaddr *peer_addr, char *hostname,
                               char *ip, mg_session *session) {
  // Populate the ip.
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
  // Populate the hostname.
  // Useful read https://stackoverflow.com/questions/12274028.
  int nameinfo_status = getnameinfo(peer_addr, sizeof(struct sockaddr),
                                    hostname, NI_MAXHOST, NULL, 0, 0);
  if (nameinfo_status != 0) {
    // ON_WINDOWS getnameinfo fails if peer_addr was constructed from
    // getaddrinfo when localhost is passed in (getaddrinfo returns an empty
    // address). I haven't find simple and clean solution to make getnameinfo
    // work. Since this function is used only to get the hostname for the
    // trust callback, setting hostname to unknown and continuing the program
    // seems sensible solution.
    DB_LOG("getnameinfo call failed. hostname set to unknown\n");
    strcpy(hostname, "unknown");
  }
  return 0;
}
#endif

int mg_connect_ca(const mg_session_params *params, mg_session **session,
                  mg_allocator *allocator) {
  // Useful read https://akkadia.org/drepper/userapi-ipv6.html.
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
  switch (params->sslmode) {
    case MG_SSLMODE_DISABLE:
      status = mg_raw_transport_init(
          sockfd, (mg_raw_transport **)&tsession->transport, allocator);
      if (status != 0) {
        mg_session_set_error(tsession, "failed to initialize connection");
        goto cleanup;
      }
      break;
#ifndef __EMSCRIPTEN__
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
        char ip[INET6_ADDRSTRLEN];
        char hostname[NI_MAXHOST];
        status = get_hostname_and_ip(&peer_addr, hostname, ip, tsession);
        if (status != 0) {
          goto cleanup;
        }
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
#endif
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
  if (sockfd >= 0 && mg_socket_close(sockfd) != 0) {
    abort();
  }
  *session = tsession;
  mg_session_invalidate(tsession);
  return status;
}

int mg_connect(const mg_session_params *params, mg_session **session) {
  return mg_connect_ca(params, session, &mg_system_allocator);
}

int handle_failure(mg_session *session) {
  int status = 0;
  status = session->version == 1 ? mg_session_send_ack_failure_message(session)
                                 : mg_session_send_reset_message(session);
  if (status != 0) {
    return status;
  }

  status = mg_session_receive_message(session);
  if (status != 0) {
    return status;
  }

  mg_message *response;
  status = mg_session_read_bolt_message(session, &response);
  if (status != 0) {
    return status;
  }

  if (response->type != MG_MESSAGE_TYPE_SUCCESS) {
    status = MG_ERROR_PROTOCOL_VIOLATION;
    mg_session_set_error(session, "unexpected message type");
  }

  mg_message_destroy_ca(response, session->decoder_allocator);
  return status;
}

int mg_session_run(mg_session *session, const char *query, const mg_map *params,
                   const mg_map *extra_run_information, const mg_list **columns,
                   int64_t *qid) {
  if (session->status == MG_SESSION_BAD) {
    mg_session_set_error(session, "bad session");
    return MG_ERROR_BAD_CALL;
  }
  if (!session->explicit_transaction &&
      session->status == MG_SESSION_EXECUTING) {
    mg_session_set_error(session, "already executing a query");
    return MG_ERROR_BAD_CALL;
  }
  if (session->status == MG_SESSION_FETCHING) {
    mg_session_set_error(session, "fetching results of a query");
    return MG_ERROR_BAD_CALL;
  }

  assert(session->status == MG_SESSION_READY ||
         (session->version == 4 && session->explicit_transaction &&
          session->status == MG_SESSION_EXECUTING));

  mg_message_destroy_ca(session->result.message, session->decoder_allocator);
  session->result.message = NULL;
  mg_list_destroy_ca(session->result.columns, session->allocator);
  session->result.columns = NULL;

  if (!params) {
    params = &mg_empty_map;
  }

  // extra field allowed only allowed for Auto-commit Transaction
  // TODO(aandelic): Check if sending extra run information while in Explicit
  // Transaction should result with an error
  if (session->version == 4 &&
      (!extra_run_information || session->explicit_transaction)) {
    extra_run_information = &mg_empty_map;
  }

  int status = 0;
  status = mg_session_send_run_message(session, query, params,
                                       extra_run_information);
  if (status != 0) {
    goto fatal_failure;
  }

  mg_transport_suspend_until_ready_to_read(session->transport);
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

    if (session->version == 4 && session->explicit_transaction) {
      if (qid) {
        const mg_value *qid_tmp =
            mg_map_at(response->success_v->metadata, "qid");

        if (!qid_tmp || mg_value_get_type(qid_tmp) != MG_VALUE_TYPE_INTEGER) {
          status = MG_ERROR_PROTOCOL_VIOLATION;
          mg_message_destroy_ca(response, session->decoder_allocator);
          mg_session_set_error(session, "invalid response metadata");
          goto fatal_failure;
        }

        *qid = mg_value_integer(qid_tmp);
      }

      ++session->query_number;
    }

    if (columns) {
      *columns = session->result.columns;
    }

    session->status = MG_SESSION_EXECUTING;
    return 0;
  }

  if (response->type == MG_MESSAGE_TYPE_FAILURE) {
    int failure_status = handle_failure_message(session, response->failure_v);

    status = handle_failure(session);
    if (status != 0) {
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

int mg_session_pull(mg_session *session, const mg_map *pull_information) {
  if (session->status == MG_SESSION_BAD) {
    mg_session_set_error(session, "called pull while bad session");
    return MG_ERROR_BAD_CALL;
  }
  if (session->status == MG_SESSION_READY) {
    mg_session_set_error(session, "called pull while not executing a query");
    return MG_ERROR_BAD_CALL;
  }
  if (session->status == MG_SESSION_FETCHING) {
    mg_session_set_error(session, "called pull while still fetching data");
    return MG_ERROR_BAD_CALL;
  }

  assert(session->status == MG_SESSION_EXECUTING);

  mg_message_destroy_ca(session->result.message, session->decoder_allocator);
  session->result.message = NULL;

  int status = 0;
  if (session->version == 4 && !pull_information) {
    pull_information = &mg_empty_map;
  }

  status = mg_session_send_pull_message(session, pull_information);
  if (status != 0) {
    goto fatal_failure;
  }

  session->status = MG_SESSION_FETCHING;
  return 0;

fatal_failure:
  mg_session_invalidate(session);
  assert(status != 0);
  return status;
}

int mg_session_fetch(mg_session *session, mg_result **result) {
  if (session->status == MG_SESSION_BAD) {
    mg_session_set_error(session, "called fetch while bad session");
    return MG_ERROR_BAD_CALL;
  }
  if (session->status == MG_SESSION_READY) {
    mg_session_set_error(session, "called fetch while not executing a query");
    return MG_ERROR_BAD_CALL;
  }
  if (session->status == MG_SESSION_EXECUTING) {
    mg_session_set_error(session, "called fetch without pulling results");
    return MG_ERROR_BAD_CALL;
  }
  assert(session->status == MG_SESSION_FETCHING);

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
    if (session->version == 4) {
      const mg_value *has_more =
          mg_map_at(message->success_v->metadata, "has_more");

      if (has_more && has_more->type != MG_VALUE_TYPE_BOOL) {
        status = MG_ERROR_PROTOCOL_VIOLATION;
        mg_message_destroy_ca(message, session->decoder_allocator);
        mg_session_set_error(session, "invalid response metadata");
        goto fatal_failure;
      }

      if (!has_more || !mg_value_bool(has_more)) {
        session->query_number -= session->explicit_transaction;
        session->status = session->explicit_transaction && session->query_number
                              ? MG_SESSION_EXECUTING
                              : MG_SESSION_READY;
      } else {
        session->status = MG_SESSION_EXECUTING;
      }
    } else {
      session->status = MG_SESSION_READY;
    }
    session->result.message = message;
    *result = &session->result;
    return 0;
  }

  if (message->type == MG_MESSAGE_TYPE_FAILURE) {
    int failure_status = handle_failure_message(session, message->failure_v);
    mg_message_destroy_ca(message, session->decoder_allocator);

    status = handle_failure(session);
    if (status != 0) {
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

int mg_session_begin_transaction(mg_session *session,
                                 const mg_map *extra_run_information) {
  if (session->version == 1) {
    mg_session_set_error(session,
                         "Transaction are not supported in this version");
  }
  if (session->status == MG_SESSION_BAD) {
    mg_session_set_error(session, "bad session");
    return MG_ERROR_BAD_CALL;
  }
  if (session->status == MG_SESSION_EXECUTING) {
    mg_session_set_error(
        session, "Cannot start a transaction while a query is executing");
    return MG_ERROR_BAD_CALL;
  }
  if (session->status == MG_SESSION_FETCHING) {
    mg_session_set_error(session, "fetching result of a query");
    return MG_ERROR_BAD_CALL;
  }
  if (session->explicit_transaction) {
    mg_session_set_error(session, "Transaction already started");
    return MG_ERROR_BAD_CALL;
  }
  assert(session->status == MG_SESSION_READY && !session->explicit_transaction);

  mg_message_destroy_ca(session->result.message, session->decoder_allocator);
  session->result.message = NULL;
  // TODO(aandelic): Check if the columns should be destroyed

  if (!extra_run_information) {
    extra_run_information = &mg_empty_map;
  }

  int status = 0;
  status = mg_session_send_begin_message(session, extra_run_information);
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
    mg_message_destroy_ca(response, session->decoder_allocator);
    session->explicit_transaction = 1;
    session->query_number = 0;
    return 0;
  }

  if (response->type == MG_MESSAGE_TYPE_FAILURE) {
    int failure_status = handle_failure_message(session, response->failure_v);

    status = handle_failure(session);
    if (status != 0) {
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

int mg_session_end_transaction(mg_session *session, int commit_transaction,
                               mg_result **result) {
  if (session->version == 1) {
    mg_session_set_error(session,
                         "Transaction are not supported in this version");
  }
  if (session->status == MG_SESSION_BAD) {
    mg_session_set_error(session, "bad session");
    return MG_ERROR_BAD_CALL;
  }

  if (!session->explicit_transaction) {
    mg_session_set_error(session, "No active transaction");
    return MG_ERROR_BAD_CALL;
  }

  if (session->status == MG_SESSION_EXECUTING ||
      session->status == MG_SESSION_FETCHING) {
    mg_session_set_error(session,
                         "Cannot end a transaction while a query is executing");
    return MG_ERROR_BAD_CALL;
  }

  assert(session->status == MG_SESSION_READY && session->explicit_transaction);

  mg_message_destroy_ca(session->result.message, session->decoder_allocator);
  session->result.message = NULL;
  // TODO(aandelic): Check if the columns should be destroyed

  int status = 0;
  status = commit_transaction ? mg_session_send_commit_messsage(session)
                              : mg_session_send_rollback_messsage(session);
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
    session->result.message = response;
    *result = &session->result;
    session->status = MG_SESSION_READY;
    session->explicit_transaction = 0;
    return 0;
  }

  if (response->type == MG_MESSAGE_TYPE_FAILURE) {
    int failure_status = handle_failure_message(session, response->failure_v);

    status = handle_failure(session);
    if (status != 0) {
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

int mg_session_commit_transaction(mg_session *session, mg_result **result) {
  return mg_session_end_transaction(session, 1, result);
}

int mg_session_rollback_transaction(mg_session *session, mg_result **result) {
  return mg_session_end_transaction(session, 0, result);
}

const mg_list *mg_result_columns(const mg_result *result) {
  return result->columns;
}

const mg_list *mg_result_row(const mg_result *result) {
  if (!result->message) {
    return NULL;
  }
  if (result->message->type != MG_MESSAGE_TYPE_RECORD) {
    return NULL;
  }
  return result->message->record_v->fields;
}

const mg_map *mg_result_summary(const mg_result *result) {
  if (!result->message) {
    return NULL;
  }
  if (result->message->type != MG_MESSAGE_TYPE_SUCCESS) {
    return NULL;
  }
  return result->message->success_v->metadata;
}
