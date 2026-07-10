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

#include "mgrouting.h"
#include "mgclient.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
      if (!addresses || mg_value_get_type(addresses) != MG_VALUE_TYPE_LIST) {
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

// ---------------------------------------------------------------------------
// Router configuration and lifecycle.
// ---------------------------------------------------------------------------

struct mg_router_config {
  const mg_session_params *session_params;  // borrowed; deep-copied by _make.
  mg_resolver_fn resolver;
  void *resolver_data;
  mg_map *routing_context;  // owned copy, or NULL.
  // Managed-transaction retry policy (see mg_router_execute_*).
  uint32_t max_retries;
  double retry_backoff;
  double retry_backoff_cap;
};

// Defaults for the managed-transaction retry policy, applied by
// mg_router_config_make and overridable via the setters.
#define MG_DEFAULT_MAX_RETRIES 8
#define MG_DEFAULT_RETRY_BACKOFF 1.0
#define MG_DEFAULT_RETRY_BACKOFF_CAP 15.0

struct mg_router {
  // Connection template, deep-copied from the seed session params so the caller
  // may free the params after mg_router_make. Exactly one of seed_host /
  // seed_address is set (the other is NULL), mirroring mg_session_params.
  char *seed_host;
  char *seed_address;
  uint16_t seed_port;
  char *username;
  char *password;
  char *user_agent;
  enum mg_sslmode sslmode;
  char *sslcert;
  char *sslkey;
  mg_trust_callback_type trust_callback;  // borrowed
  void *trust_data;                       // borrowed

  mg_resolver_fn resolver;  // borrowed; NULL means identity.
  void *resolver_data;      // borrowed
  mg_map *routing_context;  // owned copy, or NULL.

  // Cached routing table (populated by refresh) and when it expires (seconds,
  // wall clock). expires_at == 0 while no table is cached.
  mg_routing_table *table;
  time_t expires_at;
  // Round-robin cursor for READ selection.
  uint32_t read_index;

  // Managed-transaction retry policy (copied from the config).
  uint32_t max_retries;
  double retry_backoff;
  double retry_backoff_cap;

  char error[1024];
};

// strdup that treats NULL as "not set" (returns NULL). Sets *oom on failure.
static char *dup_or_null(const char *str, int *oom) {
  if (!str) {
    return NULL;
  }
  size_t size = strlen(str) + 1;
  char *copy = (char *)malloc(size);
  if (!copy) {
    *oom = 1;
    return NULL;
  }
  memcpy(copy, str, size);
  return copy;
}

mg_router_config *mg_router_config_make(void) {
  mg_router_config *config =
      (mg_router_config *)calloc(1, sizeof(mg_router_config));
  if (!config) {
    return NULL;
  }
  config->max_retries = MG_DEFAULT_MAX_RETRIES;
  config->retry_backoff = MG_DEFAULT_RETRY_BACKOFF;
  config->retry_backoff_cap = MG_DEFAULT_RETRY_BACKOFF_CAP;
  return config;
}

void mg_router_config_destroy(mg_router_config *config) {
  if (!config) {
    return;
  }
  mg_map_destroy(config->routing_context);
  free(config);
}

void mg_router_config_set_session_params(mg_router_config *config,
                                         const mg_session_params *params) {
  config->session_params = params;
}

void mg_router_config_set_resolver(mg_router_config *config,
                                   mg_resolver_fn resolver,
                                   void *resolver_data) {
  config->resolver = resolver;
  config->resolver_data = resolver_data;
}

void mg_router_config_set_routing_context(mg_router_config *config,
                                          const mg_map *routing_context) {
  mg_map_destroy(config->routing_context);
  config->routing_context =
      routing_context ? mg_map_copy(routing_context) : NULL;
}

void mg_router_config_set_max_retries(mg_router_config *config,
                                      uint32_t max_retries) {
  config->max_retries = max_retries;
}

void mg_router_config_set_retry_backoff(mg_router_config *config,
                                        double base_seconds,
                                        double cap_seconds) {
  config->retry_backoff = base_seconds;
  config->retry_backoff_cap = cap_seconds;
}

mg_router *mg_router_make(const mg_router_config *config) {
  if (!config || !config->session_params) {
    return NULL;
  }
  mg_router *router = (mg_router *)calloc(1, sizeof(mg_router));
  if (!router) {
    return NULL;
  }
  const mg_session_params *params = config->session_params;

  router->seed_port = mg_session_params_get_port(params);
  router->sslmode = mg_session_params_get_sslmode(params);
  router->trust_callback = mg_session_params_get_trust_callback(params);
  router->trust_data = mg_session_params_get_trust_data(params);
  router->resolver = config->resolver;
  router->resolver_data = config->resolver_data;
  router->max_retries = config->max_retries;
  router->retry_backoff = config->retry_backoff;
  router->retry_backoff_cap = config->retry_backoff_cap;

  int oom = 0;
  router->seed_host = dup_or_null(mg_session_params_get_host(params), &oom);
  router->seed_address =
      dup_or_null(mg_session_params_get_address(params), &oom);
  router->username = dup_or_null(mg_session_params_get_username(params), &oom);
  router->password = dup_or_null(mg_session_params_get_password(params), &oom);
  router->user_agent =
      dup_or_null(mg_session_params_get_user_agent(params), &oom);
  router->sslcert = dup_or_null(mg_session_params_get_sslcert(params), &oom);
  router->sslkey = dup_or_null(mg_session_params_get_sslkey(params), &oom);

  if (config->routing_context) {
    router->routing_context = mg_map_copy(config->routing_context);
    if (!router->routing_context) {
      oom = 1;
    }
  }

  if (oom) {
    mg_router_destroy(router);
    return NULL;
  }
  return router;
}

void mg_router_destroy(mg_router *router) {
  if (!router) {
    return;
  }
  free(router->seed_host);
  free(router->seed_address);
  free(router->username);
  free(router->password);
  free(router->user_agent);
  free(router->sslcert);
  free(router->sslkey);
  mg_map_destroy(router->routing_context);
  mg_routing_table_destroy(router->table);
  free(router);
}

// ---------------------------------------------------------------------------
// Refresh: fetch, parse and cache the routing table (with coordinator
// failover).
// ---------------------------------------------------------------------------

struct mg_resolver_result {
  mg_addr_list list;
};

int mg_resolver_result_add(mg_resolver_result *result, const char *target) {
  if (!result || !target) {
    return MG_ERROR_BAD_PARAMETER;
  }
  return addr_list_append(&result->list, target, (uint32_t)strlen(target)) == 0
             ? 0
             : MG_ERROR_OOM;
}

void mg_addr_list_clear(mg_addr_list *list) {
  for (uint32_t i = 0; i < list->size; ++i) {
    free(list->items[i]);
  }
  free(list->items);
  list->items = NULL;
  list->size = 0;
  list->capacity = 0;
}

// Resolve an advertised "host:port" into candidate targets, applying the
// router's resolver, or the identity mapping if none is set.
static int resolve_address(mg_router *router, const char *advertised,
                           mg_resolver_result *result) {
  if (router->resolver) {
    return router->resolver(advertised, result, router->resolver_data);
  }
  return mg_resolver_result_add(result, advertised);
}

// Split "host:port" (on the last ':') into a freshly allocated host and a port.
// Returns 0 on success.
static int split_host_port(const char *address, char **host_out,
                           uint16_t *port_out) {
  const char *colon = strrchr(address, ':');
  if (!colon || colon == address || colon[1] == '\0') {
    return -1;
  }
  char *end = NULL;
  long port = strtol(colon + 1, &end, 10);
  if (*end != '\0' || port < 0 || port > 65535) {
    return -1;
  }
  size_t host_len = (size_t)(colon - address);
  char *host = (char *)malloc(host_len + 1);
  if (!host) {
    return -1;
  }
  memcpy(host, address, host_len);
  host[host_len] = '\0';
  *host_out = host;
  *port_out = (uint16_t)port;
  return 0;
}

static void router_set_error(mg_router *router, const char *message) {
  snprintf(router->error, sizeof(router->error), "%s", message ? message : "");
}

// Open a connection using the router's connection template, directed at
// host/port. On failure returns NULL, stores the message in the router, and
// sets *status_out.
static mg_session *router_connect_to(mg_router *router, const char *host,
                                     uint16_t port, int use_address,
                                     int *status_out) {
  mg_session_params *params = mg_session_params_make();
  if (!params) {
    router_set_error(router, "couldn't allocate session parameters");
    *status_out = MG_ERROR_OOM;
    return NULL;
  }
  if (use_address) {
    mg_session_params_set_address(params, host);
  } else {
    mg_session_params_set_host(params, host);
  }
  mg_session_params_set_port(params, port);
  mg_session_params_set_username(params, router->username);
  mg_session_params_set_password(params, router->password);
  if (router->user_agent) {
    mg_session_params_set_user_agent(params, router->user_agent);
  }
  mg_session_params_set_sslmode(params, router->sslmode);
  mg_session_params_set_sslcert(params, router->sslcert);
  mg_session_params_set_sslkey(params, router->sslkey);
  if (router->trust_callback) {
    mg_session_params_set_trust_callback(params, router->trust_callback);
    mg_session_params_set_trust_data(params, router->trust_data);
  }

  mg_session *session = NULL;
  int status = mg_connect(params, &session);
  mg_session_params_destroy(params);
  if (status != 0) {
    router_set_error(router, mg_session_error(session));
    mg_session_destroy(session);
    *status_out = status;
    return NULL;
  }
  *status_out = 0;
  return session;
}

// Send ROUTE on an established coordinator session, parse the result, and cache
// it (replacing any previous table and resetting the TTL). Returns 0 on
// success.
static int refresh_from_session(mg_router *router, mg_session *session) {
  mg_map *empty_context = NULL;
  const mg_map *routing = router->routing_context;
  if (!routing) {
    empty_context = mg_map_make_empty(0);
    if (!empty_context) {
      router_set_error(router, "couldn't allocate routing context");
      return MG_ERROR_OOM;
    }
    routing = empty_context;
  }

  mg_map *raw = NULL;
  int status = mg_session_route(session, routing, NULL, NULL, &raw);
  mg_map_destroy(empty_context);
  if (status != 0) {
    router_set_error(router, mg_session_error(session));
    return status;
  }

  mg_routing_table *table = mg_routing_table_parse(raw);
  mg_map_destroy(raw);
  if (!table) {
    router_set_error(router, "couldn't parse routing table");
    return MG_ERROR_OOM;
  }

  mg_routing_table_destroy(router->table);
  router->table = table;
  router->expires_at = time(NULL) + (time_t)mg_routing_table_ttl(table);
  return 0;
}

int mg_router_refresh(mg_router *router) {
  if (!router) {
    return MG_ERROR_BAD_PARAMETER;
  }
  router->error[0] = '\0';
  int last_status = MG_ERROR_TRANSIENT_ERROR;

  // 1) The seed coordinator (used as given, not resolved).
  {
    const char *seed_host =
        router->seed_host ? router->seed_host : router->seed_address;
    int use_address = router->seed_host == NULL;
    int status = 0;
    mg_session *session = router_connect_to(
        router, seed_host, router->seed_port, use_address, &status);
    if (session) {
      status = refresh_from_session(router, session);
      mg_session_destroy(session);
      if (status == 0) {
        return 0;
      }
    }
    last_status = status;
  }

  // 2) Fall back to the ROUTE-role coordinators from the cached table (if any),
  //    resolved to reachable targets.
  if (router->table) {
    uint32_t count =
        mg_routing_table_address_count(router->table, MG_ROUTING_ROLE_ROUTE);
    for (uint32_t i = 0; i < count; ++i) {
      const char *advertised =
          mg_routing_table_address_at(router->table, MG_ROUTING_ROLE_ROUTE, i);
      mg_resolver_result result;
      memset(&result, 0, sizeof(result));
      if (resolve_address(router, advertised, &result) != 0) {
        mg_addr_list_clear(&result.list);
        continue;
      }
      for (uint32_t j = 0; j < result.list.size; ++j) {
        char *host = NULL;
        uint16_t port = 0;
        if (split_host_port(result.list.items[j], &host, &port) != 0) {
          continue;
        }
        int status = 0;
        mg_session *session = router_connect_to(router, host, port, 0, &status);
        free(host);
        if (session) {
          status = refresh_from_session(router, session);
          mg_session_destroy(session);
          if (status == 0) {
            mg_addr_list_clear(&result.list);
            return 0;
          }
        }
        last_status = status;
      }
      mg_addr_list_clear(&result.list);
    }
  }

  if (router->error[0] == '\0') {
    router_set_error(router,
                     "could not refresh routing table from any coordinator");
  }
  return last_status;
}

const mg_routing_table *mg_router_routing_table(mg_router *router) {
  return router ? router->table : NULL;
}

const char *mg_router_error(mg_router *router) {
  return router ? router->error : "";
}

// ---------------------------------------------------------------------------
// Connect: select a server for the access mode (round-robin for READ), resolve,
// and fail over across candidates, refreshing the table once on exhaustion.
// ---------------------------------------------------------------------------

static int addr_list_contains(const mg_addr_list *list, const char *value) {
  for (uint32_t i = 0; i < list->size; ++i) {
    if (strcmp(list->items[i], value) == 0) {
      return 1;
    }
  }
  return 0;
}

int mg_routing_select_targets(const mg_routing_table *table,
                              enum mg_routing_role role,
                              mg_resolver_fn resolver, void *resolver_data,
                              uint32_t *read_index, mg_addr_list *out) {
  if (!table) {
    return 0;
  }
  uint32_t count = mg_routing_table_address_count(table, role);
  if (count == 0) {
    return 0;
  }
  uint32_t start = 0;
  if (role == MG_ROUTING_ROLE_READ && read_index) {
    start = *read_index % count;
    *read_index += 1;
  }
  for (uint32_t k = 0; k < count; ++k) {
    const char *advertised =
        mg_routing_table_address_at(table, role, (start + k) % count);
    mg_resolver_result result;
    memset(&result, 0, sizeof(result));
    int rc = resolver ? resolver(advertised, &result, resolver_data)
                      : mg_resolver_result_add(&result, advertised);
    if (rc == 0) {
      for (uint32_t j = 0; j < result.list.size; ++j) {
        const char *target = result.list.items[j];
        if (!addr_list_contains(out, target) &&
            addr_list_append(out, target, (uint32_t)strlen(target)) != 0) {
          mg_addr_list_clear(&result.list);
          return MG_ERROR_OOM;
        }
      }
    }
    mg_addr_list_clear(&result.list);
  }
  return 0;
}

static const char *role_name(enum mg_routing_role role) {
  return role == MG_ROUTING_ROLE_WRITE ? "WRITE" : "READ";
}

static int router_connect_role(mg_router *router, enum mg_routing_role role,
                               mg_session **session_out) {
  if (!router || !session_out) {
    return MG_ERROR_BAD_PARAMETER;
  }
  *session_out = NULL;
  router->error[0] = '\0';
  int last_status = MG_ERROR_TRANSIENT_ERROR;

  // Two attempts: the second runs against a freshly refreshed table, in case
  // the topology changed (e.g. a failover) since it was cached.
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!router->table || time(NULL) >= router->expires_at) {
      int status = mg_router_refresh(router);
      if (status != 0) {
        last_status = status;  // error already recorded by refresh.
      }
    }

    mg_addr_list candidates;
    memset(&candidates, 0, sizeof(candidates));
    int select_status = mg_routing_select_targets(
        router->table, role, router->resolver, router->resolver_data,
        &router->read_index, &candidates);
    if (select_status != 0) {
      // Target selection failed (out of memory). This is not a transient
      // condition, so report it rather than falling through and retrying.
      router_set_error(router, "out of memory selecting routing targets");
      mg_addr_list_clear(&candidates);
      return select_status;
    }

    if (candidates.size == 0) {
      // Only report "no server for this role" when we actually fetched a table
      // that lacks one. If refresh failed and left no table, keep its more
      // specific error and status (e.g. the coordinator was unreachable)
      // rather than overwriting them with a misleading message.
      if (router->table) {
        char message[128];
        snprintf(message, sizeof(message), "no %s server in the routing table",
                 role_name(role));
        router_set_error(router, message);
        last_status = MG_ERROR_TRANSIENT_ERROR;
      }
    } else {
      for (uint32_t i = 0; i < candidates.size; ++i) {
        char *host = NULL;
        uint16_t port = 0;
        if (split_host_port(candidates.items[i], &host, &port) != 0) {
          continue;
        }
        int status = 0;
        mg_session *session = router_connect_to(router, host, port, 0, &status);
        free(host);
        if (session) {
          *session_out = session;
          mg_addr_list_clear(&candidates);
          return 0;
        }
        last_status = status;
      }
    }
    mg_addr_list_clear(&candidates);

    if (attempt == 0) {
      // The selected servers were unreachable (or none were listed); discard
      // the cached table and retry against a fresh one.
      mg_router_refresh(router);
    }
  }

  if (router->error[0] == '\0') {
    char message[128];
    snprintf(message, sizeof(message), "could not connect to any %s server",
             role_name(role));
    router_set_error(router, message);
  }
  return last_status;
}

int mg_router_connect_read(mg_router *router, mg_session **session) {
  return router_connect_role(router, MG_ROUTING_ROLE_READ, session);
}

int mg_router_connect_write(mg_router *router, mg_session **session) {
  return router_connect_role(router, MG_ROUTING_ROLE_WRITE, session);
}

// ---------------------------------------------------------------------------
// Managed transactions: run a unit of work with retry + capped backoff.
// ---------------------------------------------------------------------------

#ifdef _WIN32
#include <windows.h>
#endif

// Sleep for `seconds` (fractional). No-op for non-positive values.
static void router_sleep_seconds(double seconds) {
  // Skip non-positive values and NaN (NaN > 0.0 is false)
  if (!(seconds > 0.0)) {
    return;
  }
  // Clamp absurd or infinite values: no single backoff should exceed an hour.
  if (seconds > 3600.0) {
    seconds = 3600.0;
  }
#ifdef _WIN32
  Sleep((DWORD)(seconds * 1000.0));
#else
  struct timespec ts;
  ts.tv_sec = (time_t)seconds;
  ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
  nanosleep(&ts, NULL);
#endif
}

double mg_router_backoff_seconds(uint32_t attempt, double base, double cap) {
  if (attempt < 1) {
    return 0.0;
  }
  // base * 2^(attempt-1), computed by repeated doubling to avoid pow() (and the
  // math library dependency), clamping to `cap` as soon as it is reached.
  double delay = base;
  for (uint32_t i = 1; i < attempt; ++i) {
    if (delay >= cap) {
      return cap;
    }
    delay *= 2.0;
  }
  return delay > cap ? cap : delay;
}

// Runs one attempt of `work` on an established `session`. For a write, wraps it
// in an explicit transaction and commits it, treating a committed-on-main
// (SYNC-replica-unreachable) failure as success. Returns 0 on success, else a
// non-zero MG_ERROR_ code with the message copied into the router.
static int router_run_unit(mg_router *router, mg_session *session, int writing,
                           mg_work_fn work, void *work_data) {
  if (writing) {
    int status = mg_session_begin_transaction(session, NULL);
    if (status != 0) {
      router_set_error(router, mg_session_error(session));
      return status;
    }
  }

  int status = work(session, work_data);
  if (status != 0) {
    router_set_error(router, mg_session_error(session));
    if (writing) {
      mg_result *result = NULL;
      mg_session_rollback_transaction(session, &result);  // best effort
    }
    return status;
  }

  if (writing) {
    mg_result *result = NULL;
    status = mg_session_commit_transaction(session, &result);
    if (status != 0) {
      router_set_error(router, mg_session_error(session));
      return status;
    }
  }
  return 0;
}

static int router_execute(mg_router *router, enum mg_routing_role role,
                          mg_work_fn work, void *work_data) {
  if (!router || !work) {
    return MG_ERROR_BAD_PARAMETER;
  }
  router->error[0] = '\0';
  int writing = (role == MG_ROUTING_ROLE_WRITE);
  // At least one attempt, even if max_retries was set to 0.
  uint32_t max_attempts = router->max_retries > 0 ? router->max_retries : 1;
  int last_status = MG_ERROR_TRANSIENT_ERROR;

  for (uint32_t attempt = 1; attempt <= max_attempts; ++attempt) {
    mg_session *session = NULL;
    int status = router_connect_role(router, role, &session);
    if (status == 0) {
      status = router_run_unit(router, session, writing, work, work_data);
      mg_session_destroy(session);
      if (status == 0) {
        return 0;
      }
    }
    // Either connect or the unit failed; the message is already in the router.
    last_status = status;

    if (attempt == max_attempts || !mg_error_is_transient(last_status)) {
      break;
    }
    // Transient: force a routing refresh on the next attempt (so it re-routes
    // to the new main after a failover) and back off first.
    router->expires_at = 0;
    router_sleep_seconds(mg_router_backoff_seconds(
        attempt, router->retry_backoff, router->retry_backoff_cap));
  }
  return last_status;
}

int mg_router_execute_read(mg_router *router, mg_work_fn work,
                           void *work_data) {
  return router_execute(router, MG_ROUTING_ROLE_READ, work, work_data);
}

int mg_router_execute_write(mg_router *router, mg_work_fn work,
                            void *work_data) {
  return router_execute(router, MG_ROUTING_ROLE_WRITE, work, work_data);
}
