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

#ifndef MGCLIENT_MGMESSAGE_H
#define MGCLIENT_MGMESSAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "mgvalue.h"

// Some of these message types are never sent/received by client, but we still
// have them here for testing.
enum mg_message_type {
  MG_MESSAGE_TYPE_RECORD,
  MG_MESSAGE_TYPE_SUCCESS,
  MG_MESSAGE_TYPE_FAILURE,
  MG_MESSAGE_TYPE_INIT,
  MG_MESSAGE_TYPE_HELLO,
  MG_MESSAGE_TYPE_RUN,
  MG_MESSAGE_TYPE_ACK_FAILURE,
  MG_MESSAGE_TYPE_RESET,
  MG_MESSAGE_TYPE_PULL,
  MG_MESSAGE_TYPE_BEGIN,
  MG_MESSAGE_TYPE_COMMIT,
  MG_MESSAGE_TYPE_ROLLBACK
};

typedef struct mg_message_success {
  mg_map *metadata;
} mg_message_success;

typedef struct mg_message_failure {
  mg_map *metadata;
} mg_message_failure;

typedef struct mg_message_record {
  mg_list *fields;
} mg_message_record;

typedef struct mg_message_init {
  mg_string *client_name;
  mg_map *auth_token;
} mg_message_init;

typedef struct mg_message_hello {
  mg_map *extra;
} mg_message_hello;

typedef struct mg_message_run {
  mg_string *statement;
  mg_map *parameters;
  mg_map *extra;
} mg_message_run;

typedef struct mg_message_begin {
  mg_map *extra;
} mg_message_begin;

typedef struct mg_message_pull {
  mg_map *extra;
} mg_message_pull;

typedef struct mg_message {
  enum mg_message_type type;
  union {
    mg_message_success *success_v;
    mg_message_failure *failure_v;
    mg_message_record *record_v;
    mg_message_init *init_v;
    mg_message_hello *hello_v;
    mg_message_run *run_v;
    mg_message_begin *begin_v;
    mg_message_pull *pull_v;
  };
} mg_message;

void mg_message_destroy_ca(mg_message *message, mg_allocator *allocator);

#ifdef __cplusplus
}
#endif

#endif /* MGCLIENT_MGMESSAGE_H */
