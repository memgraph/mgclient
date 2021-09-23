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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mgcommon.h"
#include "mgconstants.h"
#include "mgsession.h"
#include "mgvalue.h"

int mg_session_write_uint8(mg_session *session, uint8_t val) {
  return mg_session_write_raw(session, (char *)&val, 1);
}

int mg_session_write_uint16(mg_session *session, uint16_t val) {
  val = htobe16(val);
  return mg_session_write_raw(session, (char *)&val, 2);
}

int mg_session_write_uint32(mg_session *session, uint32_t val) {
  val = htobe32(val);
  return mg_session_write_raw(session, (char *)&val, 4);
}

int mg_session_write_uint64(mg_session *session, uint64_t val) {
  val = htobe64(val);
  return mg_session_write_raw(session, (char *)&val, 8);
}

int mg_session_write_null(mg_session *session) {
  return mg_session_write_uint8(session, MG_MARKER_NULL);
}

int mg_session_write_bool(mg_session *session, int value) {
  return mg_session_write_uint8(
      session, value ? MG_MARKER_BOOL_TRUE : MG_MARKER_BOOL_FALSE);
}

int mg_session_write_integer(mg_session *session, int64_t value) {
  if (value >= MG_TINY_INT_MIN && value <= MG_TINY_INT_MAX) {
    return mg_session_write_uint8(session, (uint8_t)value);
  }
  if (value >= INT8_MIN && value <= INT8_MAX) {
    MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_MARKER_INT_8));
    return mg_session_write_uint8(session, (uint8_t)value);
  }
  if (value >= INT16_MIN && value <= INT16_MAX) {
    MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_MARKER_INT_16));
    return mg_session_write_uint16(session, (uint16_t)value);
  }
  if (value >= INT32_MIN && value <= INT32_MAX) {
    MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_MARKER_INT_32));
    return mg_session_write_uint32(session, (uint32_t)value);
  }
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_MARKER_INT_64));
  return mg_session_write_uint64(session, (uint64_t)value);
}

/// Markers have to be ordered from smallest to largest.
int mg_session_write_container_size(mg_session *session, uint32_t size,
                                    const uint8_t *markers) {
  if (size <= MG_TINY_SIZE_MAX) {
    return mg_session_write_uint8(session, (uint8_t)(markers[0] + size));
  }
  if (size <= UINT8_MAX) {
    MG_RETURN_IF_FAILED(mg_session_write_uint8(session, markers[1]));
    return mg_session_write_uint8(session, (uint8_t)size);
  }
  if (size <= UINT16_MAX) {
    MG_RETURN_IF_FAILED(mg_session_write_uint8(session, markers[2]));
    return mg_session_write_uint16(session, (uint16_t)size);
  }
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, markers[3]));
  return mg_session_write_uint32(session, size);
}

int mg_session_write_float(mg_session *session, double value) {
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_MARKER_FLOAT));
  uint64_t as_uint64;
  memcpy(&as_uint64, &value, sizeof(value));
  return mg_session_write_uint64(session, as_uint64);
}

int mg_session_write_string2(mg_session *session, uint32_t len,
                             const char *data) {
  MG_RETURN_IF_FAILED(
      mg_session_write_container_size(session, len, MG_MARKERS_STRING));
  return mg_session_write_raw(session, data, len) != 0;
}

int mg_session_write_string(mg_session *session, const char *str) {
  size_t len = strlen(str);
  if (len > UINT32_MAX) {
    mg_session_set_error(session, "string too long");
    return MG_ERROR_SIZE_EXCEEDED;
  }
  return mg_session_write_string2(session, (uint32_t)len, str);
}

int mg_session_write_list(mg_session *session, const mg_list *list) {
  MG_RETURN_IF_FAILED(
      mg_session_write_container_size(session, list->size, MG_MARKERS_LIST));
  for (uint32_t i = 0; i < list->size; ++i) {
    MG_RETURN_IF_FAILED(mg_session_write_value(session, list->elements[i]));
  }
  return 0;
}

int mg_session_write_map(mg_session *session, const mg_map *map) {
  MG_RETURN_IF_FAILED(
      mg_session_write_container_size(session, map->size, MG_MARKERS_MAP));
  for (uint32_t i = 0; i < map->size; ++i) {
    MG_RETURN_IF_FAILED(mg_session_write_string2(session, map->keys[i]->size,
                                                 map->keys[i]->data));
    MG_RETURN_IF_FAILED(mg_session_write_value(session, map->values[i]));
  }
  return 0;
}

int mg_session_write_date(mg_session *session, const mg_date *date) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT1)));
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_SIGNATURE_DATE));
  MG_RETURN_IF_FAILED(mg_session_write_integer(session, date->days));
  return 0;
}

int mg_session_write_local_time(mg_session *session, const mg_local_time *lt) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT1)));
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_SIGNATURE_LOCAL_TIME));
  MG_RETURN_IF_FAILED(mg_session_write_integer(session, lt->nanoseconds));
  return 0;
}

int mg_session_write_local_date_time(mg_session *session,
                                     const mg_local_date_time *ldt) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT2)));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_LOCAL_DATE_TIME));
  MG_RETURN_IF_FAILED(mg_session_write_integer(session, ldt->seconds));
  MG_RETURN_IF_FAILED(mg_session_write_integer(session, ldt->nanoseconds));
  return 0;
}

int mg_session_write_duration(mg_session *session, const mg_duration *dur) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT4)));
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_SIGNATURE_DURATION));
  MG_RETURN_IF_FAILED(mg_session_write_integer(session, dur->months));
  MG_RETURN_IF_FAILED(mg_session_write_integer(session, dur->days));
  MG_RETURN_IF_FAILED(mg_session_write_integer(session, dur->seconds));
  MG_RETURN_IF_FAILED(mg_session_write_integer(session, dur->nanoseconds));
  return 0;
}

int mg_session_write_value(mg_session *session, const mg_value *value) {
  switch (value->type) {
    case MG_VALUE_TYPE_NULL:
      return mg_session_write_null(session);
    case MG_VALUE_TYPE_BOOL:
      return mg_session_write_bool(session, value->bool_v);
    case MG_VALUE_TYPE_INTEGER:
      return mg_session_write_integer(session, value->integer_v);
    case MG_VALUE_TYPE_FLOAT:
      return mg_session_write_float(session, value->float_v);
    case MG_VALUE_TYPE_STRING:
      return mg_session_write_string2(session, value->string_v->size,
                                      value->string_v->data);
    case MG_VALUE_TYPE_LIST:
      return mg_session_write_list(session, value->list_v);
    case MG_VALUE_TYPE_MAP:
      return mg_session_write_map(session, value->map_v);
    case MG_VALUE_TYPE_NODE:
      mg_session_set_error(session, "tried to send value of type 'node'");
      return MG_ERROR_INVALID_VALUE;
    case MG_VALUE_TYPE_RELATIONSHIP:
      mg_session_set_error(session,
                           "tried to send value of type 'relationship'");
      return MG_ERROR_INVALID_VALUE;
    case MG_VALUE_TYPE_UNBOUND_RELATIONSHIP:
      mg_session_set_error(
          session, "tried to send value of type 'unbound_relationship'");
      return MG_ERROR_INVALID_VALUE;
    case MG_VALUE_TYPE_PATH:
      mg_session_set_error(session, "tried to send value of type 'path'");
      return MG_ERROR_INVALID_VALUE;
    case MG_VALUE_TYPE_DATE:
      return mg_session_write_date(session, value->date_v);
    case MG_VALUE_TYPE_TIME:
      mg_session_set_error(session, "tried to send value of type 'time'");
      return MG_ERROR_INVALID_VALUE;
    case MG_VALUE_TYPE_LOCAL_TIME:
      return mg_session_write_local_time(session, value->local_time_v);
    case MG_VALUE_TYPE_DATE_TIME:
      mg_session_set_error(session, "tried to send value of type 'date_time'");
      return MG_ERROR_INVALID_VALUE;
    case MG_VALUE_TYPE_DATE_TIME_ZONE_ID:
      mg_session_set_error(session,
                           "tried to send value of type 'date_time_zone_id'");
      return MG_ERROR_INVALID_VALUE;
    case MG_VALUE_TYPE_LOCAL_DATE_TIME:
      return mg_session_write_local_date_time(session,
                                              value->local_date_time_v);
    case MG_VALUE_TYPE_DURATION:
      return mg_session_write_duration(session, value->duration_v);
    case MG_VALUE_TYPE_POINT_2D:
      mg_session_set_error(session, "tried to send value of type 'point_2d'");
      return MG_ERROR_INVALID_VALUE;
    case MG_VALUE_TYPE_POINT_3D:
      mg_session_set_error(session, "tried to send value of type 'point_3d'");
      return MG_ERROR_INVALID_VALUE;
    case MG_VALUE_TYPE_UNKNOWN:
      mg_session_set_error(session, "tried to send value of unknown type");
      return MG_ERROR_INVALID_VALUE;
  }
  // Should never get here.
  abort();
}

// Some of these message types are never sent by client, but we still have
// encoding function because they are useful for testing.
int mg_session_send_init_message(mg_session *session, const char *client_name,
                                 const mg_map *auth_token) {
  size_t client_name_len = strlen(client_name);
  if (client_name_len > UINT32_MAX) {
    return MG_ERROR_SIZE_EXCEEDED;
  }
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT + 2)));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_HELLO));
  MG_RETURN_IF_FAILED(mg_session_write_string2(
      session, (uint32_t)client_name_len, client_name));
  MG_RETURN_IF_FAILED(mg_session_write_map(session, auth_token));
  return mg_session_flush_message(session);
}

int mg_session_send_hello_message(mg_session *session, const mg_map *extra) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT + 1)));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_HELLO));
  MG_RETURN_IF_FAILED(mg_session_write_map(session, extra));
  return mg_session_flush_message(session);
}

int mg_session_send_run_message(mg_session *session, const char *statement,
                                const mg_map *parameters, const mg_map *extra) {
  int field_number = 2 + (session->version == 4);
  MG_RETURN_IF_FAILED(mg_session_write_uint8(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + field_number)));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_RUN));
  MG_RETURN_IF_FAILED(mg_session_write_string(session, statement));
  MG_RETURN_IF_FAILED(mg_session_write_map(session, parameters));

  if (session->version == 4) {
    MG_RETURN_IF_FAILED(mg_session_write_map(session, extra));
  }
  return mg_session_flush_message(session);
}

int mg_session_send_pull_message(mg_session *session, const mg_map *extra) {
  uint8_t marker = MG_MARKER_TINY_STRUCT + (session->version == 4);
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, marker));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_PULL));

  if (session->version == 4) {
    MG_RETURN_IF_FAILED(mg_session_write_map(session, extra));
  }

  return mg_session_flush_message(session);
}

int mg_session_send_ack_failure_message(mg_session *session) {
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_MARKER_TINY_STRUCT));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_ACK_FAILURE));
  return mg_session_flush_message(session);
}

int mg_session_send_reset_message(mg_session *session) {
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, MG_MARKER_TINY_STRUCT));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_RESET));
  return mg_session_flush_message(session);
}

int mg_session_send_failure_message(mg_session *session,
                                    const mg_map *metadata) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT + 1)));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_FAILURE));
  MG_RETURN_IF_FAILED(mg_session_write_map(session, metadata));
  return mg_session_flush_message(session);
}

int mg_session_send_success_message(mg_session *session,
                                    const mg_map *metadata) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT + 1)));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_SUCCESS));
  MG_RETURN_IF_FAILED(mg_session_write_map(session, metadata));
  return mg_session_flush_message(session);
}

int mg_session_send_record_message(mg_session *session, const mg_list *fields) {
  MG_RETURN_IF_FAILED(mg_session_write_uint8(session, 0xB1));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_RECORD));
  MG_RETURN_IF_FAILED(mg_session_write_list(session, fields));
  return mg_session_flush_message(session);
}

int mg_session_send_begin_message(mg_session *session, const mg_map *extra) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT + 1)));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_BEGIN));
  MG_RETURN_IF_FAILED(mg_session_write_map(session, extra));
  return mg_session_flush_message(session);
}

int mg_session_send_commit_messsage(mg_session *session) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT)));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_COMMIT));
  return mg_session_flush_message(session);
}

int mg_session_send_rollback_messsage(mg_session *session) {
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, (uint8_t)(MG_MARKER_TINY_STRUCT)));
  MG_RETURN_IF_FAILED(
      mg_session_write_uint8(session, MG_SIGNATURE_MESSAGE_ROLLBACK));
  return mg_session_flush_message(session);
}
