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

#include <string.h>

#include "mgclient.h"
#include "mgcommon.h"
#include "mgconstants.h"
#include "mgmessage.h"
#include "mgsession.h"
#include "mgvalue.h"

int mg_session_read_uint8(mg_session *session, uint8_t *val) {
  if (session->in_cursor + 1 > session->in_end) {
    mg_session_set_error(session, "unexpected end of message");
    return MG_ERROR_DECODING_FAILED;
  }
  *val = *(uint8_t *)(session->in_buffer + session->in_cursor);
  session->in_cursor += 1;
  return 0;
}

int mg_session_read_uint16(mg_session *session, uint16_t *val) {
  if (session->in_cursor + 2 > session->in_end) {
    mg_session_set_error(session, "unexpected end of message");
    return MG_ERROR_DECODING_FAILED;
  }
  *val = be16toh(*(uint16_t *)(session->in_buffer + session->in_cursor));
  session->in_cursor += 2;
  return 0;
}

int mg_session_read_uint32(mg_session *session, uint32_t *val) {
  if (session->in_cursor + 4 > session->in_end) {
    mg_session_set_error(session, "unexpected end of message");
    return MG_ERROR_DECODING_FAILED;
  }
  *val = be32toh(*(uint32_t *)(session->in_buffer + session->in_cursor));
  session->in_cursor += 4;
  return 0;
}

int mg_session_read_uint64(mg_session *session, uint64_t *val) {
  if (session->in_cursor + 8 > session->in_end) {
    mg_session_set_error(session, "unexpected end of message");
    return MG_ERROR_DECODING_FAILED;
  }
  *val = be64toh(*(uint64_t *)(session->in_buffer + session->in_cursor));
  session->in_cursor += 8;
  return 0;
}

int mg_session_read_null(mg_session *session) {
  uint8_t marker;
  MG_RETURN_IF_FAILED(mg_session_read_uint8(session, &marker));
  if (marker != MG_MARKER_NULL) {
    mg_session_set_error(session, "wrong value marker");
    return MG_ERROR_DECODING_FAILED;
  }
  return 0;
}

int mg_session_read_integer(mg_session *session, int64_t *val) {
  uint8_t marker;
  MG_RETURN_IF_FAILED(mg_session_read_uint8(session, &marker));

  if ((marker & 0x80) == 0) {
    *val = marker;
    return 0;
  }

  if ((marker & 0xF0) == 0xF0) {
    *val = (int64_t)marker - 256;
    return 0;
  }

  switch (marker) {
    case MG_MARKER_INT_8: {
      uint8_t tmp;
      MG_RETURN_IF_FAILED(mg_session_read_uint8(session, &tmp));
      *val = *(int8_t *)&tmp;
      return 0;
    }
    case MG_MARKER_INT_16: {
      uint16_t tmp;
      MG_RETURN_IF_FAILED(mg_session_read_uint16(session, &tmp));
      *val = *(int16_t *)&tmp;
      return 0;
    }
    case MG_MARKER_INT_32: {
      uint32_t tmp;
      MG_RETURN_IF_FAILED(mg_session_read_uint32(session, &tmp));
      *val = *(int32_t *)&tmp;
      return 0;
    }
    case MG_MARKER_INT_64: {
      uint64_t tmp;
      MG_RETURN_IF_FAILED(mg_session_read_uint64(session, &tmp));
      *val = *(int64_t *)&tmp;
      return 0;
    }
  }

  mg_session_set_error(session, "wrong value marker");
  return MG_ERROR_DECODING_FAILED;
}

int mg_session_read_bool(mg_session *session, int *value) {
  uint8_t marker;
  MG_RETURN_IF_FAILED(mg_session_read_uint8(session, &marker));

  switch (marker) {
    case MG_MARKER_BOOL_FALSE:
      *value = 0;
      return 0;
    case MG_MARKER_BOOL_TRUE:
      *value = 1;
      return 0;
  }

  mg_session_set_error(session, "wrong value marker");
  return MG_ERROR_DECODING_FAILED;
}

int mg_session_read_float(mg_session *session, double *value) {
  uint8_t marker;
  MG_RETURN_IF_FAILED(mg_session_read_uint8(session, &marker));
  if (marker != MG_MARKER_FLOAT) {
    mg_session_set_error(session, "wrong value marker");
    return MG_ERROR_DECODING_FAILED;
  }
  uint64_t as_uint64;
  MG_RETURN_IF_FAILED(mg_session_read_uint64(session, &as_uint64));
  memcpy(value, &as_uint64, sizeof(double));
  return 0;
}

/// Markers have to be ordered from smallest to largest.
int mg_session_read_container_size(mg_session *session, uint32_t *size,
                                   const uint8_t *markers) {
  uint8_t marker;
  MG_RETURN_IF_FAILED(mg_session_read_uint8(session, &marker));

  if ((marker & 0xF0) == markers[0]) {
    *size = marker & 0xF;
    return 0;
  }

  if (marker == markers[1]) {
    uint8_t tmp;
    MG_RETURN_IF_FAILED(mg_session_read_uint8(session, &tmp));
    *size = tmp;
  } else if (marker == markers[2]) {
    uint16_t tmp;
    MG_RETURN_IF_FAILED(mg_session_read_uint16(session, &tmp));
    *size = tmp;
  } else if (marker == markers[3]) {
    uint32_t tmp;
    MG_RETURN_IF_FAILED(mg_session_read_uint32(session, &tmp));
    *size = tmp;
  } else {
    mg_session_set_error(session, "wrong value marker");
    return MG_ERROR_DECODING_FAILED;
  }

  return 0;
}

int mg_session_read_string(mg_session *session, mg_string **str) {
  uint32_t size;
  MG_RETURN_IF_FAILED(
      mg_session_read_container_size(session, &size, MG_MARKERS_STRING));

  if (session->in_cursor + size > session->in_end) {
    mg_session_set_error(session, "unexpected end of message");
    return MG_ERROR_DECODING_FAILED;
  }

  mg_string *tstr = mg_string_alloc(size, session->decoder_allocator);
  if (!tstr) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  tstr->size = size;
  memcpy(tstr->data, session->in_buffer + session->in_cursor, size);
  session->in_cursor += size;
  *str = tstr;
  return 0;
}

int mg_session_read_list(mg_session *session, mg_list **list) {
  uint32_t size;
  MG_RETURN_IF_FAILED(
      mg_session_read_container_size(session, &size, MG_MARKERS_LIST));

  mg_list *tlist = mg_list_alloc(size, session->decoder_allocator);
  if (!tlist) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;

  tlist->size = 0;
  for (uint32_t i = 0; i < size; ++i) {
    status = mg_session_read_value(session, &tlist->elements[i]);
    if (status != 0) {
      goto cleanup;
    }
    tlist->size++;
  }

  *list = tlist;
  return 0;

cleanup:
  for (uint32_t i = 0; i < tlist->size; ++i) {
    mg_value_destroy_ca(tlist->elements[i], session->decoder_allocator);
  }
  mg_allocator_free(session->decoder_allocator, tlist);
  return status;
}

int mg_session_read_map(mg_session *session, mg_map **map) {
  uint32_t size;
  MG_RETURN_IF_FAILED(
      mg_session_read_container_size(session, &size, MG_MARKERS_MAP));

  mg_map *tmap = mg_map_alloc(size, session->decoder_allocator);
  if (!tmap) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;

  tmap->size = size;

  uint32_t keys_read = 0;
  uint32_t values_read = 0;
  for (uint32_t i = 0; i < size; ++i) {
    status = mg_session_read_string(session, &tmap->keys[i]);
    if (status != 0) {
      goto cleanup;
    }
    keys_read++;
    status = mg_session_read_value(session, &tmap->values[i]);
    if (status != 0) {
      goto cleanup;
    }
    values_read++;
  }

  *map = tmap;
  return 0;

cleanup:
  for (uint32_t i = 0; i < keys_read; ++i) {
    mg_string_destroy_ca(tmap->keys[i], session->decoder_allocator);
  }
  for (uint32_t i = 0; i < values_read; ++i) {
    mg_value_destroy_ca(tmap->values[i], session->decoder_allocator);
  }
  mg_allocator_free(session->decoder_allocator, tmap);
  return status;
}

int mg_session_check_struct_header(mg_session *session, uint8_t marker,
                                   uint8_t signature) {
  if (session->in_cursor + 2 > session->in_end) {
    mg_session_set_error(session, "unexpected end of message");
    return MG_ERROR_DECODING_FAILED;
  }
  uint8_t *header = (uint8_t *)(session->in_buffer + session->in_cursor);
  if (header[0] != marker) {
    mg_session_set_error(session, "wrong value marker");
    return MG_ERROR_DECODING_FAILED;
  }
  if (header[1] != signature) {
    mg_session_set_error(session, "wrong struct signature");
    return MG_ERROR_DECODING_FAILED;
  }
  session->in_cursor += 2;
  return 0;
}

int mg_session_read_node(mg_session *session, mg_node **node) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 3), MG_SIGNATURE_NODE));

  int64_t id;
  MG_RETURN_IF_FAILED(mg_session_read_integer(session, &id));

  uint32_t label_count;
  MG_RETURN_IF_FAILED(
      mg_session_read_container_size(session, &label_count, MG_MARKERS_LIST));

  mg_node *tnode = mg_node_alloc(label_count, session->decoder_allocator);
  if (!tnode) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;

  tnode->id = id;
  tnode->label_count = 0;
  for (uint32_t i = 0; i < label_count; ++i) {
    status = mg_session_read_string(session, &tnode->labels[i]);
    if (status != 0) {
      goto cleanup;
    }
    tnode->label_count++;
  }

  status = mg_session_read_map(session, &tnode->properties);
  if (status != 0) {
    goto cleanup;
  }

  *node = tnode;

  return 0;

cleanup:
  for (uint32_t i = 0; i < tnode->label_count; ++i) {
    mg_string_destroy_ca(tnode->labels[i], session->decoder_allocator);
  }
  mg_allocator_free(session->decoder_allocator, tnode);
  return status;
}

int mg_session_read_relationship(mg_session *session, mg_relationship **rel) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 5),
      MG_SIGNATURE_RELATIONSHIP));

  mg_relationship *trel =
      mg_allocator_malloc(session->decoder_allocator, sizeof(mg_relationship));
  if (!trel) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;

  status = mg_session_read_integer(session, &trel->id);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &trel->start_id);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &trel->end_id);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_string(session, &trel->type);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_map(session, &trel->properties);
  if (status != 0) {
    goto cleanup_type;
  }

  *rel = trel;
  return 0;

cleanup_type:
  mg_string_destroy_ca(trel->type, session->decoder_allocator);

cleanup:
  mg_allocator_free(session->decoder_allocator, trel);
  return status;
}

int mg_session_read_unbound_relationship(mg_session *session,
                                         mg_unbound_relationship **rel) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 3),
      MG_SIGNATURE_UNBOUND_RELATIONSHIP));

  mg_unbound_relationship *trel = mg_allocator_malloc(
      session->decoder_allocator, sizeof(mg_unbound_relationship));
  if (!trel) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;

  status = mg_session_read_integer(session, &trel->id);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_string(session, &trel->type);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_map(session, &trel->properties);
  if (status != 0) {
    goto cleanup_type;
  }

  *rel = trel;
  return 0;

cleanup_type:
  mg_string_destroy_ca(trel->type, session->decoder_allocator);

cleanup:
  mg_allocator_free(session->decoder_allocator, trel);
  return status;
}

int mg_session_read_path(mg_session *session, mg_path **path) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 3), MG_SIGNATURE_PATH));

  uint32_t node_count;
  MG_RETURN_IF_FAILED(
      mg_session_read_container_size(session, &node_count, MG_MARKERS_LIST));

  // There must be at least one node in the node list.
  if (!node_count) {
    mg_session_set_error(session, "invalid path: empty node list");
    return MG_ERROR_DECODING_FAILED;
  }

  mg_node **nodes = mg_allocator_malloc(session->decoder_allocator,
                                        node_count * sizeof(mg_node *));
  if (!nodes) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;

  uint32_t nodes_read = 0;
  for (uint32_t i = 0; i < node_count; ++i) {
    status = mg_session_read_node(session, &nodes[i]);
    if (status != 0) {
      goto cleanup_nodes;
    }
    nodes_read++;
  }

  uint32_t relationship_count;
  status = mg_session_read_container_size(session, &relationship_count,
                                          MG_MARKERS_LIST);
  if (status != 0) {
    goto cleanup_nodes;
  }

  mg_unbound_relationship **relationships = mg_allocator_malloc(
      session->decoder_allocator,
      relationship_count * sizeof(mg_unbound_relationship *));
  if (!relationships) {
    mg_session_set_error(session, "out of memory");
    status = MG_ERROR_OOM;
    goto cleanup_nodes;
  }

  uint32_t relationships_read = 0;
  for (uint32_t i = 0; i < relationship_count; ++i) {
    status = mg_session_read_unbound_relationship(session, &relationships[i]);
    if (status != 0) {
      goto cleanup_relationships;
    }
    relationships_read++;
  }

  uint32_t sequence_length;
  status = mg_session_read_container_size(session, &sequence_length,
                                          MG_MARKERS_LIST);
  if (status != 0) {
    goto cleanup_relationships;
  }
  // Path is an alternating sequence of nodes and relationships starting and
  // ending with a node, so it's length must be odd. First node on the path is
  // implicit so sequence length must be even.
  if (sequence_length % 2 != 0) {
    mg_session_set_error(session, "invalid path: odd sequence length");
    status = MG_ERROR_DECODING_FAILED;
    goto cleanup_relationships;
  }

  int64_t *sequence = mg_allocator_malloc(session->decoder_allocator,
                                          sequence_length * sizeof(int64_t));
  if (!sequence) {
    mg_session_set_error(session, "out of memory");
    status = MG_ERROR_OOM;
    goto cleanup_relationships;
  }

  for (uint32_t i = 0; i < sequence_length; ++i) {
    status = mg_session_read_integer(session, &sequence[i]);
    if (status != 0) {
      goto cleanup_sequence;
    }
    if (i % 2 == 0) {
      // Relationships are 1-indexed with sign determining direction.
      int64_t idx = sequence[i] > 0 ? sequence[i] : -sequence[i];
      if (idx < 1 || idx > relationship_count) {
        mg_session_set_error(session,
                             "invalid path: relationship index out of range");
        status = MG_ERROR_DECODING_FAILED;
        goto cleanup_sequence;
      }
    } else {
      // Nodes are 0-indexed.
      if (sequence[i] < 0 || sequence[i] >= node_count) {
        mg_session_set_error(session, "invalid path: node index out of range");
        status = MG_ERROR_DECODING_FAILED;
        goto cleanup_sequence;
      }
    }
  }

  mg_path *tpath = mg_path_alloc(node_count, relationship_count,
                                 sequence_length, session->decoder_allocator);
  if (!tpath) {
    mg_session_set_error(session, "out of memory");
    status = MG_ERROR_OOM;
    goto cleanup_sequence;
  }

  tpath->node_count = node_count;
  memcpy(tpath->nodes, nodes, node_count * sizeof(mg_node *));
  mg_allocator_free(session->decoder_allocator, nodes);

  tpath->relationship_count = relationship_count;
  memcpy(tpath->relationships, relationships,
         relationship_count * sizeof(mg_unbound_relationship *));
  mg_allocator_free(session->decoder_allocator, relationships);

  tpath->sequence_length = sequence_length;
  memcpy(tpath->sequence, sequence, sequence_length * sizeof(int64_t));
  mg_allocator_free(session->decoder_allocator, sequence);

  *path = tpath;
  return 0;

cleanup_sequence:
  mg_allocator_free(session->decoder_allocator, sequence);

cleanup_relationships:
  for (uint32_t i = 0; i < relationships_read; ++i) {
    mg_unbound_relationship_destroy_ca(relationships[i],
                                       session->decoder_allocator);
  }
  mg_allocator_free(session->decoder_allocator, relationships);

cleanup_nodes:
  for (uint32_t i = 0; i < nodes_read; ++i) {
    mg_node_destroy_ca(nodes[i], session->decoder_allocator);
  }
  mg_allocator_free(session->decoder_allocator, nodes);
  return status;
}

int mg_session_read_date(mg_session *session, mg_date **date) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 1), MG_SIGNATURE_DATE));
  mg_date *date_tmp = mg_date_alloc(session->decoder_allocator);
  if (!date_tmp) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;
  status = mg_session_read_integer(session, &date_tmp->days);
  if (status != 0) {
    goto cleanup;
  }

  *date = date_tmp;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, date_tmp);
  return status;
}

int mg_session_read_time(mg_session *session, mg_time **time) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 2), MG_SIGNATURE_TIME));
  mg_time *time_tmp = mg_time_alloc(session->decoder_allocator);
  if (!time_tmp) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;
  status = mg_session_read_integer(session, &time_tmp->nanoseconds);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &time_tmp->tz_offset_seconds);
  if (status != 0) {
    goto cleanup;
  }

  *time = time_tmp;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, time_tmp);
  return status;
}

int mg_session_read_local_time(mg_session *session,
                               mg_local_time **local_time) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 1), MG_SIGNATURE_LOCAL_TIME));
  mg_local_time *local_time_tmp =
      mg_local_time_alloc(session->decoder_allocator);
  if (!local_time_tmp) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;
  status = mg_session_read_integer(session, &local_time_tmp->nanoseconds);
  if (status != 0) {
    goto cleanup;
  }

  *local_time = local_time_tmp;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, local_time_tmp);
  return status;
}

int mg_session_read_date_time(mg_session *session, mg_date_time **date_time) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 3), MG_SIGNATURE_DATE_TIME));
  mg_date_time *date_time_tmp = mg_date_time_alloc(session->decoder_allocator);
  if (!date_time_tmp) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;
  status = mg_session_read_integer(session, &date_time_tmp->seconds);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &date_time_tmp->nanoseconds);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &date_time_tmp->tz_offset_minutes);
  if (status != 0) {
    goto cleanup;
  }

  *date_time = date_time_tmp;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, date_time_tmp);
  return status;
}

int mg_session_read_date_time_zone_id(
    mg_session *session, mg_date_time_zone_id **date_time_zone_id) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 3),
      MG_SIGNATURE_DATE_TIME_ZONE_ID));
  mg_date_time_zone_id *date_time_zone_id_tmp =
      mg_date_time_zone_id_alloc(session->decoder_allocator);
  if (!date_time_zone_id_tmp) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;
  status = mg_session_read_integer(session, &date_time_zone_id_tmp->seconds);
  if (status != 0) {
    goto cleanup;
  }

  status =
      mg_session_read_integer(session, &date_time_zone_id_tmp->nanoseconds);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &date_time_zone_id_tmp->tz_id);

  *date_time_zone_id = date_time_zone_id_tmp;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, date_time_zone_id_tmp);
  return status;
}

int mg_session_read_local_date_time(mg_session *session,
                                    mg_local_date_time **local_date_time) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 2),
      MG_SIGNATURE_LOCAL_DATE_TIME));
  mg_local_date_time *local_date_time_tmp =
      mg_local_date_time_alloc(session->decoder_allocator);
  if (!local_date_time_tmp) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;
  status = mg_session_read_integer(session, &local_date_time_tmp->seconds);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &local_date_time_tmp->nanoseconds);
  if (status != 0) {
    goto cleanup;
  }

  *local_date_time = local_date_time_tmp;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, local_date_time_tmp);
  return status;
}

int mg_session_read_duration(mg_session *session, mg_duration **duration) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 4), MG_SIGNATURE_DURATION));
  mg_duration *duration_tmp = mg_duration_alloc(session->decoder_allocator);
  if (!duration_tmp) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;
  status = mg_session_read_integer(session, &duration_tmp->months);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &duration_tmp->days);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &duration_tmp->seconds);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_integer(session, &duration_tmp->nanoseconds);
  if (status != 0) {
    goto cleanup;
  }

  *duration = duration_tmp;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, duration_tmp);
  return status;
}

int mg_session_read_point_2d(mg_session *session, mg_point_2d **point_2d) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 3), MG_SIGNATURE_POINT_2D));
  mg_point_2d *point_2d_tmp = mg_point_2d_alloc(session->decoder_allocator);
  if (!point_2d_tmp) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;
  status = mg_session_read_integer(session, &point_2d_tmp->srid);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_float(session, &point_2d_tmp->x);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_float(session, &point_2d_tmp->y);
  if (status != 0) {
    goto cleanup;
  }

  *point_2d = point_2d_tmp;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, point_2d_tmp);
  return status;
}

int mg_session_read_point_3d(mg_session *session, mg_point_3d **point_3d) {
  MG_RETURN_IF_FAILED(mg_session_check_struct_header(
      session, (uint8_t)(MG_MARKER_TINY_STRUCT + 4), MG_SIGNATURE_POINT_3D));
  mg_point_3d *point_3d_tmp = mg_point_3d_alloc(session->decoder_allocator);
  if (!point_3d_tmp) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;
  status = mg_session_read_integer(session, &point_3d_tmp->srid);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_float(session, &point_3d_tmp->x);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_float(session, &point_3d_tmp->y);
  if (status != 0) {
    goto cleanup;
  }

  status = mg_session_read_float(session, &point_3d_tmp->z);
  if (status != 0) {
    goto cleanup;
  }

  *point_3d = point_3d_tmp;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, point_3d_tmp);
  return status;
}

int mg_session_read_struct_value(mg_session *session, mg_value *value) {
  if (session->in_cursor + 2 > session->in_end) {
    mg_session_set_error(session, "unexpected end of message");
    return MG_ERROR_DECODING_FAILED;
  }
  uint8_t *header = (uint8_t *)(session->in_buffer + session->in_cursor);
  uint8_t marker = (uint8_t)header[0];
  uint8_t signature = (uint8_t)header[1];

  if ((marker & 0xF0) != MG_MARKER_TINY_STRUCT) {
    mg_session_set_error(session, "unsupported value");
    return MG_ERROR_DECODING_FAILED;
  }

  switch (signature) {
    case MG_SIGNATURE_NODE:
      value->type = MG_VALUE_TYPE_NODE;
      return mg_session_read_node(session, &value->node_v);
    case MG_SIGNATURE_RELATIONSHIP:
      value->type = MG_VALUE_TYPE_RELATIONSHIP;
      return mg_session_read_relationship(session, &value->relationship_v);
    case MG_SIGNATURE_UNBOUND_RELATIONSHIP:
      value->type = MG_VALUE_TYPE_UNBOUND_RELATIONSHIP;
      return mg_session_read_unbound_relationship(
          session, &value->unbound_relationship_v);
    case MG_SIGNATURE_PATH:
      value->type = MG_VALUE_TYPE_PATH;
      return mg_session_read_path(session, &value->path_v);
    case MG_SIGNATURE_DATE:
      value->type = MG_VALUE_TYPE_DATE;
      return mg_session_read_date(session, &value->date_v);
    case MG_SIGNATURE_TIME:
      value->type = MG_VALUE_TYPE_TIME;
      return mg_session_read_time(session, &value->time_v);
    case MG_SIGNATURE_LOCAL_TIME:
      value->type = MG_VALUE_TYPE_LOCAL_TIME;
      return mg_session_read_local_time(session, &value->local_time_v);
    case MG_SIGNATURE_DATE_TIME:
      value->type = MG_VALUE_TYPE_DATE_TIME;
      return mg_session_read_date_time(session, &value->date_time_v);
    case MG_SIGNATURE_DATE_TIME_ZONE_ID:
      value->type = MG_VALUE_TYPE_DATE_TIME_ZONE_ID;
      return mg_session_read_date_time_zone_id(session,
                                               &value->date_time_zone_id_v);
    case MG_SIGNATURE_LOCAL_DATE_TIME:
      value->type = MG_VALUE_TYPE_LOCAL_DATE_TIME;
      return mg_session_read_local_date_time(session,
                                             &value->local_date_time_v);
    case MG_SIGNATURE_DURATION:
      value->type = MG_VALUE_TYPE_DURATION;
      return mg_session_read_duration(session, &value->duration_v);
    case MG_SIGNATURE_POINT_2D:
      value->type = MG_VALUE_TYPE_POINT_2D;
      return mg_session_read_point_2d(session, &value->point_2d_v);
    case MG_SIGNATURE_POINT_3D:
      value->type = MG_VALUE_TYPE_POINT_3D;
      return mg_session_read_point_3d(session, &value->point_3d_v);
  }

  mg_session_set_error(session, "unsupported value");
  return MG_ERROR_DECODING_FAILED;
}

int mg_session_read_value(mg_session *session, mg_value **value) {
  if (session->in_cursor >= session->in_end) {
    mg_session_set_error(session, "unexpected end of message");
    return MG_ERROR_DECODING_FAILED;
  }
  uint8_t marker = *(uint8_t *)(session->in_buffer + session->in_cursor);

  mg_value *tvalue =
      mg_allocator_malloc(session->decoder_allocator, sizeof(mg_value));
  if (!tvalue) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;

  switch (marker) {
    case MG_MARKER_NULL: {
      tvalue->type = MG_VALUE_TYPE_NULL;
      status = mg_session_read_null(session);
      if (status != 0) {
        goto cleanup;
      }
      break;
    }
    case MG_MARKER_BOOL_FALSE:
    case MG_MARKER_BOOL_TRUE:
      tvalue->type = MG_VALUE_TYPE_BOOL;
      status = mg_session_read_bool(session, &tvalue->bool_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_MARKER_INT_8:
    case MG_MARKER_INT_16:
    case MG_MARKER_INT_32:
    case MG_MARKER_INT_64:
      tvalue->type = MG_VALUE_TYPE_INTEGER;
      status = mg_session_read_integer(session, &tvalue->integer_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_MARKER_FLOAT:
      tvalue->type = MG_VALUE_TYPE_FLOAT;
      status = mg_session_read_float(session, &tvalue->float_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_MARKER_STRING_8:
    case MG_MARKER_STRING_16:
    case MG_MARKER_STRING_32:
      tvalue->type = MG_VALUE_TYPE_STRING;
      status = mg_session_read_string(session, &tvalue->string_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_MARKER_LIST_8:
    case MG_MARKER_LIST_16:
    case MG_MARKER_LIST_32:
      tvalue->type = MG_VALUE_TYPE_LIST;
      status = mg_session_read_list(session, &tvalue->list_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_MARKER_MAP_8:
    case MG_MARKER_MAP_16:
    case MG_MARKER_MAP_32:
      tvalue->type = MG_VALUE_TYPE_MAP;
      status = mg_session_read_map(session, &tvalue->map_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_MARKER_STRUCT_8:
    case MG_MARKER_STRUCT_16:
      mg_session_set_error(session, "unsupported value");
      status = MG_ERROR_DECODING_FAILED;
      goto cleanup;
    default:
      if ((marker & 0x80) == 0 || (marker & 0xF0) == 0xF0) {
        tvalue->type = MG_VALUE_TYPE_INTEGER;
        status = mg_session_read_integer(session, &tvalue->integer_v);
        if (status != 0) {
          goto cleanup;
        }
      } else if ((marker & 0xF0) == MG_MARKER_TINY_STRING) {
        tvalue->type = MG_VALUE_TYPE_STRING;
        status = mg_session_read_string(session, &tvalue->string_v);
        if (status != 0) {
          goto cleanup;
        }
      } else if ((marker & 0xF0) == MG_MARKER_TINY_LIST) {
        tvalue->type = MG_VALUE_TYPE_LIST;
        status = mg_session_read_list(session, &tvalue->list_v);
        if (status != 0) {
          goto cleanup;
        }
      } else if ((marker & 0xF0) == MG_MARKER_TINY_MAP) {
        tvalue->type = MG_VALUE_TYPE_MAP;
        status = mg_session_read_map(session, &tvalue->map_v);
        if (status != 0) {
          goto cleanup;
        }
      } else if ((marker & 0xF0) == MG_MARKER_TINY_STRUCT) {
        status = mg_session_read_struct_value(session, tvalue);
        if (status != 0) {
          goto cleanup;
        }
      } else {
        mg_session_set_error(session, "unsupported value");
        status = MG_ERROR_DECODING_FAILED;
        goto cleanup;
      }
  }

  *value = tvalue;
  return 0;

cleanup:
  mg_allocator_free(session->decoder_allocator, tvalue);
  return status;
}

// Some of these message types are never received by client, but we still have
// decoding function because they are useful for testing.
int mg_session_read_success_message(mg_session *session,
                                    mg_message_success **message) {
  mg_map *metadata;
  MG_RETURN_IF_FAILED(mg_session_read_map(session, &metadata));

  mg_message_success *tmessage = mg_allocator_malloc(
      session->decoder_allocator, sizeof(mg_message_success));
  if (!tmessage) {
    mg_session_set_error(session, "out of memory");
    mg_map_destroy(metadata);
    return MG_ERROR_OOM;
  }
  tmessage->metadata = metadata;
  *message = tmessage;
  return 0;
}

int mg_session_read_record_message(mg_session *session,
                                   mg_message_record **message) {
  mg_list *fields;
  MG_RETURN_IF_FAILED(mg_session_read_list(session, &fields));

  mg_message_record *tmessage = mg_allocator_malloc(session->decoder_allocator,
                                                    sizeof(mg_message_record));
  if (!tmessage) {
    mg_session_set_error(session, "out of memory");
    mg_list_destroy(fields);
    return MG_ERROR_OOM;
  }
  tmessage->fields = fields;
  *message = tmessage;
  return 0;
}

int mg_session_read_failure_message(mg_session *session,
                                    mg_message_failure **message) {
  mg_map *metadata;
  MG_RETURN_IF_FAILED(mg_session_read_map(session, &metadata));

  mg_message_failure *tmessage = mg_allocator_malloc(
      session->decoder_allocator, sizeof(mg_message_failure));
  if (!tmessage) {
    mg_session_set_error(session, "out of memory");
    mg_map_destroy(metadata);
    return MG_ERROR_OOM;
  }
  tmessage->metadata = metadata;
  *message = tmessage;
  return 0;
}

int mg_session_read_init_message(mg_session *session,
                                 mg_message_init **message) {
  mg_string *client_name;
  MG_RETURN_IF_FAILED(mg_session_read_string(session, &client_name));

  int status = 0;

  mg_map *auth_token;
  status = mg_session_read_map(session, &auth_token);
  if (status != 0) {
    mg_string_destroy_ca(client_name, session->decoder_allocator);
    goto cleanup_client_name;
  }

  mg_message_init *tmessage =
      mg_allocator_malloc(session->decoder_allocator, sizeof(mg_message_init));
  if (!tmessage) {
    status = MG_ERROR_OOM;
    goto cleanup;
  }

  tmessage->client_name = client_name;
  tmessage->auth_token = auth_token;
  *message = tmessage;
  return 0;

cleanup:
  mg_map_destroy_ca(auth_token, session->decoder_allocator);

cleanup_client_name:
  mg_string_destroy_ca(client_name, session->decoder_allocator);
  return status;
}

int mg_session_read_hello_message(mg_session *session,
                                  mg_message_hello **message) {
  int status = 0;

  mg_map *extra;
  status = mg_session_read_map(session, &extra);
  if (status != 0) {
    return status;
  }

  mg_message_hello *tmessage =
      mg_allocator_malloc(session->decoder_allocator, sizeof(mg_message_hello));
  if (!tmessage) {
    status = MG_ERROR_OOM;
    goto cleanup;
  }

  tmessage->extra = extra;
  *message = tmessage;
  return 0;

cleanup:
  mg_map_destroy_ca(extra, session->decoder_allocator);
  return status;
}

int mg_session_read_run_message(mg_session *session, mg_message_run **message) {
  mg_string *statement;
  MG_RETURN_IF_FAILED(mg_session_read_string(session, &statement));

  int status = 0;

  mg_map *parameters;
  status = mg_session_read_map(session, &parameters);
  if (status != 0) {
    mg_string_destroy_ca(statement, session->decoder_allocator);
    goto cleanup_statement;
  }

  mg_map *extra = NULL;
  if (session->version == 4) {
    status = mg_session_read_map(session, &extra);
    if (status != 0) {
      goto cleanup_parameters;
    }
  }

  mg_message_run *tmessage =
      mg_allocator_malloc(session->decoder_allocator, sizeof(mg_message_run));
  if (!tmessage) {
    status = MG_ERROR_OOM;
    goto cleanup;
  }

  tmessage->statement = statement;
  tmessage->parameters = parameters;
  tmessage->extra = extra;

  *message = tmessage;
  return 0;

cleanup:
  mg_map_destroy_ca(extra, session->decoder_allocator);

cleanup_parameters:
  mg_map_destroy_ca(parameters, session->decoder_allocator);

cleanup_statement:
  mg_string_destroy_ca(statement, session->decoder_allocator);
  return status;
}

int mg_session_read_begin_message(mg_session *session,
                                  mg_message_begin **message) {
  int status = 0;

  mg_map *extra;
  status = mg_session_read_map(session, &extra);
  if (status != 0) {
    return status;
  }

  mg_message_begin *tmessage =
      mg_allocator_malloc(session->decoder_allocator, sizeof(mg_message_begin));
  if (!tmessage) {
    status = MG_ERROR_OOM;
    goto cleanup;
  }

  tmessage->extra = extra;
  *message = tmessage;

cleanup:
  mg_map_destroy_ca(extra, session->decoder_allocator);
  return status;
}

int mg_session_read_pull_message(mg_session *session,
                                 mg_message_pull **message) {
  int status = 0;

  mg_map *extra = NULL;
  if (session->version == 4) {
    status = mg_session_read_map(session, &extra);
    if (status != 0) {
      return status;
    }
  }

  mg_message_pull *tmessage =
      mg_allocator_malloc(session->decoder_allocator, sizeof(mg_message_pull));
  if (!tmessage) {
    status = MG_ERROR_OOM;
    goto cleanup;
  }

  tmessage->extra = extra;
  *message = tmessage;

cleanup:
  mg_map_destroy_ca(extra, session->decoder_allocator);
  return status;
}

int mg_session_read_bolt_message(mg_session *session, mg_message **message) {
  uint8_t marker;
  MG_RETURN_IF_FAILED(mg_session_read_uint8(session, &marker));

  uint8_t signature;
  MG_RETURN_IF_FAILED(mg_session_read_uint8(session, &signature));

  mg_message *tmessage =
      mg_allocator_malloc(session->decoder_allocator, sizeof(mg_message));
  if (!tmessage) {
    mg_session_set_error(session, "out of memory");
    return MG_ERROR_OOM;
  }

  int status = 0;

  switch (signature) {
    case MG_SIGNATURE_MESSAGE_SUCCESS:
      if (marker != (uint8_t)(MG_MARKER_TINY_STRUCT + 1)) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_SUCCESS;
      status = mg_session_read_success_message(session, &tmessage->success_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_SIGNATURE_MESSAGE_FAILURE:
      if (marker != (uint8_t)(MG_MARKER_TINY_STRUCT + 1)) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_FAILURE;
      status = mg_session_read_failure_message(session, &tmessage->failure_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_SIGNATURE_MESSAGE_RECORD:
      if (marker != (uint8_t)(MG_MARKER_TINY_STRUCT + 1)) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_RECORD;
      status = mg_session_read_record_message(session, &tmessage->record_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_SIGNATURE_MESSAGE_HELLO:
      if (session->version == 1) {
        if (marker != (uint8_t)(MG_MARKER_TINY_STRUCT + 2)) {
          goto wrong_marker;
        }
        tmessage->type = MG_MESSAGE_TYPE_INIT;
        status = mg_session_read_init_message(session, &tmessage->init_v);
      } else {
        if (marker != (uint8_t)(MG_MARKER_TINY_STRUCT + 1)) {
          goto wrong_marker;
        }
        tmessage->type = MG_MESSAGE_TYPE_HELLO;
        status = mg_session_read_hello_message(session, &tmessage->hello_v);
      }
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_SIGNATURE_MESSAGE_RUN: {
      int field_number = 2 + (session->version == 4);
      if (marker != (uint8_t)(MG_MARKER_TINY_STRUCT + field_number)) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_RUN;
      status = mg_session_read_run_message(session, &tmessage->run_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    }
    case MG_SIGNATURE_MESSAGE_ACK_FAILURE:
      if (marker != MG_MARKER_TINY_STRUCT) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_ACK_FAILURE;
      break;
    case MG_SIGNATURE_MESSAGE_BEGIN:
      if (marker != (uint8_t)(MG_MARKER_TINY_STRUCT + 1)) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_BEGIN;
      status = mg_session_read_begin_message(session, &tmessage->begin_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    case MG_SIGNATURE_MESSAGE_COMMIT:
      if (marker != (uint8_t)(MG_MARKER_TINY_STRUCT)) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_COMMIT;
      break;
    case MG_SIGNATURE_MESSAGE_ROLLBACK:
      if (marker != (uint8_t)(MG_MARKER_TINY_STRUCT)) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_ROLLBACK;
      break;
    case MG_SIGNATURE_MESSAGE_RESET:
      if (marker != MG_MARKER_TINY_STRUCT) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_RESET;
      break;
    case MG_SIGNATURE_MESSAGE_PULL: {
      uint8_t expected_marker = MG_MARKER_TINY_STRUCT + (session->version == 4);
      if (marker != expected_marker) {
        goto wrong_marker;
      }
      tmessage->type = MG_MESSAGE_TYPE_PULL;
      status = mg_session_read_pull_message(session, &tmessage->pull_v);
      if (status != 0) {
        goto cleanup;
      }
      break;
    }
    default:
      mg_session_set_error(session, "unknown message type");
      status = MG_ERROR_PROTOCOL_VIOLATION;
      goto cleanup;
  }

  *message = tmessage;
  return 0;

wrong_marker:
  mg_session_set_error(session, "wrong value marker");
  status = MG_ERROR_PROTOCOL_VIOLATION;
cleanup:
  mg_allocator_free(session->decoder_allocator, tmessage);
  return status;
}
