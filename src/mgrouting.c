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

#include "mgclient.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// A growable array of owned, NUL-terminated address strings.
typedef struct {
  char **items;
  uint32_t size;
  uint32_t capacity;
} mg_addr_list;

struct mg_routing_table {
  int64_t ttl;
  // Addresses grouped by role, indexed by enum mg_routing_role.
  mg_addr_list roles[3];
};

static int role_is_valid(enum mg_routing_role role) {
  return role == MG_ROUTING_ROLE_READ || role == MG_ROUTING_ROLE_WRITE ||
         role == MG_ROUTING_ROLE_ROUTE;
}

// Map a role string (not NUL-terminated) to the enum. Returns 0 on success.
static int role_from_string(const mg_string *str, enum mg_routing_role *out) {
  const char *data = mg_string_data(str);
  uint32_t size = mg_string_size(str);
  if (size == 4 && memcmp(data, "READ", 4) == 0) {
    *out = MG_ROUTING_ROLE_READ;
    return 0;
  }
  if (size == 5 && memcmp(data, "WRITE", 5) == 0) {
    *out = MG_ROUTING_ROLE_WRITE;
    return 0;
  }
  if (size == 5 && memcmp(data, "ROUTE", 5) == 0) {
    *out = MG_ROUTING_ROLE_ROUTE;
    return 0;
  }
  return -1;
}

// Append a copy of `size` bytes of `data` (NUL-terminating it) to `list`.
static int addr_list_append(mg_addr_list *list, const char *data,
                            uint32_t size) {
  if (list->size == list->capacity) {
    uint32_t new_capacity = list->capacity ? list->capacity * 2 : 4;
    char **new_items =
        (char **)realloc(list->items, new_capacity * sizeof(char *));
    if (!new_items) {
      return -1;
    }
    list->items = new_items;
    list->capacity = new_capacity;
  }
  char *copy = (char *)malloc((size_t)size + 1);
  if (!copy) {
    return -1;
  }
  memcpy(copy, data, size);
  copy[size] = '\0';
  list->items[list->size++] = copy;
  return 0;
}

mg_routing_table *mg_routing_table_parse(const mg_map *raw) {
  if (!raw) {
    return NULL;
  }
  mg_routing_table *table =
      (mg_routing_table *)calloc(1, sizeof(mg_routing_table));
  if (!table) {
    return NULL;
  }

  const mg_value *ttl = mg_map_at(raw, "ttl");
  if (ttl && mg_value_get_type(ttl) == MG_VALUE_TYPE_INTEGER) {
    table->ttl = mg_value_integer(ttl);
  }

  const mg_value *servers = mg_map_at(raw, "servers");
  if (servers && mg_value_get_type(servers) == MG_VALUE_TYPE_LIST) {
    const mg_list *server_list = mg_value_list(servers);
    uint32_t server_count = mg_list_size(server_list);
    for (uint32_t i = 0; i < server_count; ++i) {
      const mg_value *server_value = mg_list_at(server_list, i);
      if (!server_value ||
          mg_value_get_type(server_value) != MG_VALUE_TYPE_MAP) {
        continue;
      }
      const mg_map *server = mg_value_map(server_value);

      const mg_value *role_value = mg_map_at(server, "role");
      if (!role_value ||
          mg_value_get_type(role_value) != MG_VALUE_TYPE_STRING) {
        continue;
      }
      enum mg_routing_role role;
      if (role_from_string(mg_value_string(role_value), &role) != 0) {
        continue;  // Unrecognised role -- ignore this server.
      }

      const mg_value *addresses = mg_map_at(server, "addresses");
      if (!addresses ||
          mg_value_get_type(addresses) != MG_VALUE_TYPE_LIST) {
        continue;
      }
      const mg_list *address_list = mg_value_list(addresses);
      uint32_t address_count = mg_list_size(address_list);
      for (uint32_t j = 0; j < address_count; ++j) {
        const mg_value *address = mg_list_at(address_list, j);
        if (!address || mg_value_get_type(address) != MG_VALUE_TYPE_STRING) {
          continue;
        }
        const mg_string *str = mg_value_string(address);
        if (addr_list_append(&table->roles[role], mg_string_data(str),
                              mg_string_size(str)) != 0) {
          mg_routing_table_destroy(table);
          return NULL;
        }
      }
    }
  }

  return table;
}

void mg_routing_table_destroy(mg_routing_table *table) {
  if (!table) {
    return;
  }
  for (size_t r = 0; r < sizeof(table->roles) / sizeof(table->roles[0]); ++r) {
    for (uint32_t i = 0; i < table->roles[r].size; ++i) {
      free(table->roles[r].items[i]);
    }
    free(table->roles[r].items);
  }
  free(table);
}

int64_t mg_routing_table_ttl(const mg_routing_table *table) {
  return table ? table->ttl : 0;
}

uint32_t mg_routing_table_address_count(const mg_routing_table *table,
                                        enum mg_routing_role role) {
  if (!table || !role_is_valid(role)) {
    return 0;
  }
  return table->roles[role].size;
}

const char *mg_routing_table_address_at(const mg_routing_table *table,
                                        enum mg_routing_role role,
                                        uint32_t index) {
  if (!table || !role_is_valid(role) || index >= table->roles[role].size) {
    return NULL;
  }
  return table->roles[role].items[index];
}

int mg_error_is_transient(int error) {
  switch (error) {
    // The server told us so (Bolt "TransientError" category).
    case MG_ERROR_TRANSIENT_ERROR:
    // Low-level transport/connection failures: no Bolt code, but retryable in
    // an HA cluster (an instance dropped mid-request, or was momentarily
    // unreachable during a failover). Non-transport failures such as
    // MG_ERROR_BAD_PARAMETER, MG_ERROR_DECODING_FAILED,
    // MG_ERROR_PROTOCOL_VIOLATION and MG_ERROR_SSL_ERROR are deliberately
    // excluded.
    case MG_ERROR_SEND_FAILED:
    case MG_ERROR_RECV_FAILED:
    case MG_ERROR_NETWORK_FAILURE:
    case MG_ERROR_SOCKET:
      return 1;
    default:
      return 0;
  }
}

// Case-insensitive substring search. `needle` is matched regardless of case;
// strcasestr is a non-standard extension so we roll our own for portability.
static int contains_ci(const char *haystack, const char *needle) {
  if (!haystack || !needle) {
    return 0;
  }
  size_t needle_len = strlen(needle);
  if (needle_len == 0) {
    return 1;
  }
  for (const char *h = haystack; *h; ++h) {
    size_t i = 0;
    while (i < needle_len && h[i] &&
           tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) {
      ++i;
    }
    if (i == needle_len) {
      return 1;
    }
  }
  return 0;
}

int mg_error_is_committed_on_main(const char *message) {
  return contains_ci(message, "replication exception") &&
         contains_ci(message, "committed on the main");
}
