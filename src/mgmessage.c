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

#include "mgmessage.h"

void mg_message_success_destroy_ca(mg_message_success *message,
                                   mg_allocator *allocator) {
  if (!message) return;
  mg_map_destroy_ca(message->metadata, allocator);
  mg_allocator_free(allocator, message);
}

void mg_message_failure_destroy_ca(mg_message_failure *message,
                                   mg_allocator *allocator) {
  if (!message) return;
  mg_map_destroy_ca(message->metadata, allocator);
  mg_allocator_free(allocator, message);
}

void mg_message_record_destroy_ca(mg_message_record *message,
                                  mg_allocator *allocator) {
  if (!message) return;
  mg_list_destroy_ca(message->fields, allocator);
  mg_allocator_free(allocator, message);
}

void mg_message_init_destroy_ca(mg_message_init *message,
                                mg_allocator *allocator) {
  if (!message) return;
  mg_string_destroy_ca(message->client_name, allocator);
  mg_map_destroy_ca(message->auth_token, allocator);
  mg_allocator_free(allocator, message);
}

void mg_message_hello_destroy_ca(mg_message_hello *message,
                                 mg_allocator *allocator) {
  if (!message) return;
  mg_map_destroy_ca(message->extra, allocator);
  mg_allocator_free(allocator, message);
}

void mg_message_run_destroy_ca(mg_message_run *message,
                               mg_allocator *allocator) {
  if (!message) return;
  mg_string_destroy_ca(message->statement, allocator);
  mg_map_destroy_ca(message->parameters, allocator);
  mg_map_destroy_ca(message->extra, allocator);
  mg_allocator_free(allocator, message);
}

void mg_message_begin_destroy_ca(mg_message_begin *message,
                                 mg_allocator *allocator) {
  if (!message) {
    return;
  }
  mg_map_destroy_ca(message->extra, allocator);
  mg_allocator_free(allocator, message);
}

void mg_message_pull_destroy_ca(mg_message_pull *message,
                                mg_allocator *allocator) {
  if (!message) {
    return;
  }
  mg_map_destroy_ca(message->extra, allocator);
  mg_allocator_free(allocator, message);
}

void mg_message_destroy_ca(mg_message *message, mg_allocator *allocator) {
  if (!message) return;
  switch (message->type) {
    case MG_MESSAGE_TYPE_SUCCESS:
      mg_message_success_destroy_ca(message->success_v, allocator);
      break;
    case MG_MESSAGE_TYPE_FAILURE:
      mg_message_failure_destroy_ca(message->failure_v, allocator);
      break;
    case MG_MESSAGE_TYPE_RECORD:
      mg_message_record_destroy_ca(message->record_v, allocator);
      break;
    case MG_MESSAGE_TYPE_INIT:
      mg_message_init_destroy_ca(message->init_v, allocator);
      break;
    case MG_MESSAGE_TYPE_HELLO:
      mg_message_hello_destroy_ca(message->hello_v, allocator);
      break;
    case MG_MESSAGE_TYPE_RUN:
      mg_message_run_destroy_ca(message->run_v, allocator);
      break;
    case MG_MESSAGE_TYPE_BEGIN:
      mg_message_begin_destroy_ca(message->begin_v, allocator);
      break;
    case MG_MESSAGE_TYPE_PULL:
      mg_message_pull_destroy_ca(message->pull_v, allocator);
      break;
    case MG_MESSAGE_TYPE_ACK_FAILURE:
    case MG_MESSAGE_TYPE_RESET:
    case MG_MESSAGE_TYPE_COMMIT:
    case MG_MESSAGE_TYPE_ROLLBACK:
      break;
  }
  mg_allocator_free(allocator, message);
}
