// Copyright (c) 2016-2026 Memgraph Ltd. [https://memgraph.com]
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

// Internal routing helpers. NOT part of the public API -- exposed only so the
// (white-box) unit tests can drive the pure selection logic without a cluster.

#ifndef MGCLIENT_MGROUTING_H
#define MGCLIENT_MGROUTING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "mgclient.h"

// A growable array of owned, NUL-terminated address strings.
typedef struct {
  char **items;
  uint32_t size;
  uint32_t capacity;
} mg_addr_list;

// Frees the contents of `list` and resets it to empty.
void mg_addr_list_clear(mg_addr_list *list);

// Fills `out` (which must be zero-initialised) with the resolved candidate
// "host:port" targets for `role`, in the order they should be tried.
//
// For MG_ROUTING_ROLE_READ, selection starts at (*read_index % count) and
// *read_index is post-incremented, giving round-robin across replicas while
// keeping the remaining replicas as failover candidates; WRITE and ROUTE are
// returned in table order and `read_index` is left untouched. Each advertised
// address is mapped through `resolver` (with `resolver_data`), or the identity
// mapping when `resolver` is NULL. Duplicate targets are skipped.
//
// Returns 0 on success, or MG_ERROR_OOM on allocation failure.
int mg_routing_select_targets(const mg_routing_table *table,
                              enum mg_routing_role role,
                              mg_resolver_fn resolver, void *resolver_data,
                              uint32_t *read_index, mg_addr_list *out);

// The capped-exponential backoff delay (in seconds) to wait before the retry
// that follows attempt `attempt` (1-based): min(base * 2^(attempt-1), cap).
// Returns 0 for attempt < 1. Exposed for white-box testing of the retry policy
// used by mg_router_execute_read / mg_router_execute_write.
double mg_router_backoff_seconds(uint32_t attempt, double base, double cap);

#ifdef __cplusplus
}
#endif

#endif  // MGCLIENT_MGROUTING_H
