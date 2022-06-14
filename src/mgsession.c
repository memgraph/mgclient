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

#include "mgsession.h"

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "mgcommon.h"
#include "mgconstants.h"
#include "mgtransport.h"

int mg_session_status(const mg_session *session) {
  if (!session) {
    return MG_SESSION_BAD;
  }
  return session->status;
}

#define MG_DECODER_ALLOCATOR_BLOCK_SIZE 131072
// This seems like a reasonable value --- at most 4 kB per block is wasted
// (around 3% of allocated memory, not including padding), and separate
// allocations should only happen for really big objects (lists with more than
// 500 elements, maps with more than 250 elements, strings with more than 4000
// characters, ...).
#define MG_DECODER_SEP_ALLOC_THRESHOLD 4096

mg_session *mg_session_init(mg_allocator *allocator) {
  mg_linear_allocator *decoder_allocator =
      mg_linear_allocator_init(allocator, MG_DECODER_ALLOCATOR_BLOCK_SIZE,
                               MG_DECODER_SEP_ALLOC_THRESHOLD);
  if (!decoder_allocator) {
    return NULL;
  }

  mg_session *session = mg_allocator_malloc(allocator, sizeof(mg_session));
  if (!session) {
    mg_linear_allocator_destroy(decoder_allocator);
    return NULL;
  }

  session->transport = NULL;
  session->allocator = allocator;
  session->decoder_allocator = (mg_allocator *)decoder_allocator;
  session->out_buffer = NULL;
  session->in_buffer = NULL;
  session->out_capacity = MG_BOLT_CHUNK_HEADER_SIZE + MG_BOLT_MAX_CHUNK_SIZE;
  session->out_buffer = mg_allocator_malloc(allocator, session->out_capacity);
  if (!session->out_buffer) {
    goto cleanup;
  }
  session->out_begin = MG_BOLT_CHUNK_HEADER_SIZE;
  session->out_end = session->out_begin;

  session->in_capacity = MG_BOLT_MAX_CHUNK_SIZE;
  session->in_buffer = mg_allocator_malloc(allocator, session->in_capacity);
  if (!session->in_buffer) {
    goto cleanup;
  }
  session->in_end = 0;
  session->in_cursor = 0;

  session->result.session = session;
  session->result.message = NULL;
  session->result.columns = NULL;

  session->explicit_transaction = 0;
  session->query_number = 0;

  session->error_buffer[0] = 0;

  return session;

cleanup:
  mg_linear_allocator_destroy(decoder_allocator);
  mg_allocator_free(allocator, session->in_buffer);
  mg_allocator_free(allocator, session->out_buffer);
  mg_allocator_free(allocator, session);
  return NULL;
}

void mg_session_set_error(mg_session *session, const char *fmt, ...) {
  va_list arglist;
  va_start(arglist, fmt);
  if (vsnprintf(session->error_buffer, MG_MAX_ERROR_SIZE, fmt, arglist) < 0) {
    strncpy(session->error_buffer, "couldn't set error message",
            MG_MAX_ERROR_SIZE);
  }
  va_end(arglist);
}

const char *mg_session_error(mg_session *session) {
  if (!session) {
    return "session is NULL (possibly out of memory)";
  }
  return session->error_buffer;
}

void mg_session_invalidate(mg_session *session) {
  if (session->transport) {
    mg_transport_destroy(session->transport);
    session->transport = NULL;
  }
  session->status = MG_SESSION_BAD;
}

void mg_session_destroy(mg_session *session) {
  if (!session) {
    return;
  }
  if (session->transport) {
    mg_transport_destroy(session->transport);
  }
  mg_allocator_free(session->allocator, session->in_buffer);
  mg_allocator_free(session->allocator, session->out_buffer);

  mg_message_destroy_ca(session->result.message, session->decoder_allocator);
  session->result.message = NULL;
  mg_list_destroy_ca(session->result.columns, session->allocator);
  session->result.columns = NULL;

  mg_linear_allocator_destroy(
      (mg_linear_allocator *)session->decoder_allocator);
  mg_allocator_free(session->allocator, session);
}

int mg_session_flush_chunk(mg_session *session) {
  size_t chunk_size = session->out_end - session->out_begin;
  if (!chunk_size) {
    return 0;
  }
  if (chunk_size > MG_BOLT_MAX_CHUNK_SIZE) {
    abort();
  }

  // Actual chunk data is written with offset of two bytes, leaving 2 bytes for
  // chunk size which we write here before sending.
  assert(session->out_begin == MG_BOLT_CHUNK_HEADER_SIZE);
  assert(MG_BOLT_CHUNK_HEADER_SIZE == sizeof(uint16_t));

  *(uint16_t *)session->out_buffer = htobe16((uint16_t)chunk_size);

  if (mg_transport_send(session->transport, session->out_buffer,
                        session->out_end) != 0) {
    mg_session_set_error(session, "failed to send chunk data");
    return MG_ERROR_SEND_FAILED;
  }

  session->out_end = session->out_begin;
  return 0;
}

int mg_session_flush_message(mg_session *session) {
  {
    int status = mg_session_flush_chunk(session);
    if (status != 0) {
      return status;
    }
  }
  const char MESSAGE_END[] = {0x00, 0x00};
  {
    int status =
        mg_transport_send(session->transport, MESSAGE_END, sizeof(MESSAGE_END));
    if (status != 0) {
      mg_session_set_error(session, "failed to send message end marker");
      return MG_ERROR_SEND_FAILED;
    }
  }
  return 0;
}

int mg_session_write_raw(mg_session *session, const char *data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    size_t buffer_free = session->out_capacity - session->out_end;
    if (len - sent >= buffer_free) {
      memcpy(session->out_buffer + session->out_end, data + sent, buffer_free);
      session->out_end = session->out_capacity;
      sent += buffer_free;
      {
        int status = mg_session_flush_chunk(session);
        if (status != 0) {
          return status;
        }
      }
    } else {
      memcpy(session->out_buffer + session->out_end, data + sent, len - sent);
      session->out_end += len - sent;
      sent = len;
    }
  }
  return 0;
}

int mg_session_ensure_space_for_chunk(mg_session *session, size_t chunk_size) {
  while (session->in_capacity - session->in_end < chunk_size) {
    char *new_in_buffer = mg_allocator_realloc(
        session->allocator, session->in_buffer, 2 * session->in_capacity);
    if (!new_in_buffer) {
      mg_session_set_error(session,
                           "failed to enlarge incoming message buffer");
      return MG_ERROR_OOM;
    }
    session->in_capacity = 2 * session->in_capacity;
    session->in_buffer = new_in_buffer;
  }
  return 0;
}

int mg_session_read_chunk(mg_session *session) {
  uint16_t chunk_size;
  mg_transport_suspend_until_ready_to_read(session->transport);
  if (mg_transport_recv(session->transport, (char *)&chunk_size, 2) != 0) {
    mg_session_set_error(session, "failed to receive chunk size");
    return MG_ERROR_RECV_FAILED;
  }
  chunk_size = be16toh(chunk_size);
  if (chunk_size == 0) {
    return 0;
  }
  {
    int status = mg_session_ensure_space_for_chunk(session, chunk_size);
    if (status != 0) {
      return status;
    }
  }
  mg_transport_suspend_until_ready_to_read(session->transport);
  if (mg_transport_recv(session->transport,
                        session->in_buffer + session->in_end,
                        chunk_size) != 0) {
    mg_session_set_error(session, "failed to receive chunk data");
    return MG_ERROR_RECV_FAILED;
  }
  session->in_end += chunk_size;
  return 1;
}

int mg_session_receive_message(mg_session *session) {
  // At this point, we reset the session decoder allocator and all objects from
  // the previous message are lost.
  mg_linear_allocator_reset((mg_linear_allocator *)session->decoder_allocator);
  session->in_end = 0;
  session->in_cursor = 0;
  int status;
  do {
    status = mg_session_read_chunk(session);
  } while (status == 1);
  return status;
}
