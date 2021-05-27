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

#ifndef MGCLIENT_MGSESSION_H
#define MGCLIENT_MGSESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "mgconstants.h"
#include "mgmessage.h"
#include "mgtransport.h"
#include "mgvalue.h"

#define MG_MAX_ERROR_SIZE 1024

typedef struct mg_result {
  int status;
  mg_session *session;
  mg_message *message;
  mg_list *columns;
} mg_result;

typedef struct mg_session {
  int status;

  int explicit_transaction;
  int query_number;

  mg_transport *transport;

  int version;

  char *out_buffer;
  size_t out_begin;
  size_t out_end;
  size_t out_capacity;

  char *in_buffer;
  size_t in_end;
  size_t in_capacity;
  size_t in_cursor;

  mg_result result;

  char error_buffer[MG_MAX_ERROR_SIZE];

  mg_allocator *allocator;
  mg_allocator *decoder_allocator;
} mg_session;

mg_session *mg_session_init(mg_allocator *allocator);

void mg_session_invalidate(mg_session *session);

void mg_session_set_error(mg_session *session, const char *fmt, ...);

void mg_session_destroy(mg_session *session);

int mg_session_write_raw(mg_session *session, const char *data, size_t len);

int mg_session_flush_message(mg_session *session);

int mg_session_write_uint8(mg_session *session, uint8_t val);

int mg_session_write_uint16(mg_session *session, uint16_t val);

int mg_session_write_uint32(mg_session *session, uint32_t val);

int mg_session_write_uint64(mg_session *session, uint64_t val);

int mg_session_write_null(mg_session *session);

int mg_session_write_bool(mg_session *session, int value);

int mg_session_write_integer(mg_session *session, int64_t value);

int mg_session_write_float(mg_session *session, double value);

int mg_session_write_string(mg_session *session, const char *str);

int mg_session_write_string2(mg_session *session, uint32_t len,
                             const char *data);

int mg_session_write_list(mg_session *session, const mg_list *list);

int mg_session_write_map(mg_session *session, const mg_map *map);

int mg_session_write_value(mg_session *session, const mg_value *value);

int mg_session_receive_message(mg_session *session);

void *mg_session_allocate(mg_session *session, size_t size);

int mg_session_read_integer(mg_session *session, int64_t *val);

int mg_session_read_bool(mg_session *session, int *val);

int mg_session_read_float(mg_session *session, double *value);

int mg_session_read_string(mg_session *session, mg_string **str);

int mg_session_read_list(mg_session *session, mg_list **list);

int mg_session_read_map(mg_session *session, mg_map **map);

int mg_session_read_node(mg_session *session, mg_node **node);

int mg_session_read_relationship(mg_session *session, mg_relationship **rel);

int mg_session_read_unbound_relationship(mg_session *session,
                                         mg_unbound_relationship **rel);

int mg_session_read_path(mg_session *session, mg_path **path);

int mg_session_read_date(mg_session *session, mg_date **date);

int mg_session_read_time(mg_session *session, mg_time **time);

int mg_session_read_local_time(mg_session *session, mg_local_time **local_time);

int mg_session_read_date_time(mg_session *session, mg_date_time **date_time);

int mg_session_read_date_time_zone_id(mg_session *session,
                                      mg_date_time_zone_id **date_time_zone_id);

int mg_session_read_local_date_time(mg_session *session,
                                    mg_local_date_time **local_date_time);

int mg_session_read_duration(mg_session *session, mg_duration **duration);

int mg_session_read_point_2d(mg_session *session, mg_point_2d **point_2d);

int mg_session_read_point_3d(mg_session *session, mg_point_3d **point_3d);

int mg_session_read_value(mg_session *session, mg_value **value);

int mg_session_read_bolt_message(mg_session *session, mg_message **message);

// Some of these message types are never sent by client, but send functions are
// still here for testing.
int mg_session_send_init_message(mg_session *session, const char *client_name,
                                 const mg_map *auth_token);

int mg_session_send_hello_message(mg_session *session, const mg_map *extra);

int mg_session_send_run_message(mg_session *session, const char *statement,
                                const mg_map *parameters, const mg_map *extra);

int mg_session_send_pull_message(mg_session *session, const mg_map *extra);

int mg_session_send_reset_message(mg_session *session);

int mg_session_send_ack_failure_message(mg_session *session);

int mg_session_send_failure_message(mg_session *session,
                                    const mg_map *metadata);

int mg_session_send_success_message(mg_session *session,
                                    const mg_map *metadata);

int mg_session_send_record_message(mg_session *session, const mg_list *fields);

int mg_session_send_begin_message(mg_session *session, const mg_map *extra);

int mg_session_send_commit_messsage(mg_session *session);

int mg_session_send_rollback_messsage(mg_session *session);

#ifdef __cplusplus
}
#endif

#endif /* MGCLIENT_MGSESSION_H */
