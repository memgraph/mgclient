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

// Client-side routing against a Memgraph high-availability (HA) cluster.
//
// You give the router the address of a *coordinator*. It fetches the cluster's
// routing table (which instance is the main, which are replicas), caches it
// until its TTL expires, and hands out connections to the right instance:
// writes go to the main, reads to a replica.
//
// The managed helpers -- mg_router_execute_write / mg_router_execute_read --
// go one step further: they run your unit of work and, if the cluster is
// briefly in flux (for example while a new main is being elected during a
// failover), they refresh the routing table and retry with exponential
// back-off. So a write issued the instant the old main goes down still lands
// on the new main once it is elected, without any special handling in your
// code.
//
// Usage: routing <coordinator-host> <coordinator-port> [username] [password]

#include <stdio.h>
#include <stdlib.h>

#include <mgclient.h>

// Drain any remaining rows in the current result stream. Returns 0 on success,
// or a negative MG_ERROR_ code if a fetch failed.
static int drain(mg_session *session) {
  mg_result *result;
  int status;
  while ((status = mg_session_fetch(session, &result)) == 1) {
  }
  return status;
}

// A managed *write*: create/update a greeting node.
//
// The router owns the transaction around this callback, so the work must NOT
// begin/commit it -- it only issues the query. Because the work may be called
// more than once (on retry), it is written to be idempotent: MERGE rather than
// CREATE, so a retry never duplicates the node.
static int write_greeting(mg_session *session, void *data) {
  (void)data;
  int status = mg_session_run(
      session, "MERGE (n:Greeting {id: 1}) SET n.text = 'hello from routing'",
      NULL, NULL, NULL, NULL);
  if (status != 0) {
    return status;
  }
  if ((status = mg_session_pull(session, NULL)) != 0) {
    return status;
  }
  return drain(session);
}

// A managed *read*: read the greeting text back into the caller's buffer.
static int read_greeting(mg_session *session, void *data) {
  char *out = (char *)data;
  int status = mg_session_run(session, "MATCH (n:Greeting {id: 1}) RETURN n.text",
                              NULL, NULL, NULL, NULL);
  if (status != 0) {
    return status;
  }
  if ((status = mg_session_pull(session, NULL)) != 0) {
    return status;
  }

  mg_result *result;
  while ((status = mg_session_fetch(session, &result)) == 1) {
    const mg_list *row = mg_result_row(result);
    if (mg_list_size(row) > 0) {
      const mg_value *value = mg_list_at(row, 0);
      if (mg_value_get_type(value) == MG_VALUE_TYPE_STRING) {
        const mg_string *text = mg_value_string(value);
        snprintf(out, 256, "%.*s", (int)mg_string_size(text),
                 mg_string_data(text));
      }
    }
  }
  return status;  // 0 once the stream is fully consumed
}

static void print_routing_table(mg_router *router) {
  if (mg_router_refresh(router) != 0) {
    printf("could not fetch routing table: %s\n", mg_router_error(router));
    return;
  }
  const mg_routing_table *table = mg_router_routing_table(router);
  printf("routing table (ttl %lld s):\n",
         (long long)mg_routing_table_ttl(table));

  const struct {
    enum mg_routing_role role;
    const char *name;
  } roles[] = {{MG_ROUTING_ROLE_WRITE, "WRITE"},
               {MG_ROUTING_ROLE_READ, "READ"},
               {MG_ROUTING_ROLE_ROUTE, "ROUTE"}};
  for (size_t r = 0; r < sizeof(roles) / sizeof(roles[0]); ++r) {
    uint32_t count = mg_routing_table_address_count(table, roles[r].role);
    for (uint32_t i = 0; i < count; ++i) {
      printf("  %-5s %s\n", roles[r].name,
             mg_routing_table_address_at(table, roles[r].role, i));
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3 || argc > 5) {
    fprintf(stderr,
            "Usage: %s <coordinator-host> <coordinator-port> "
            "[username] [password]\n",
            argv[0]);
    return 1;
  }

  mg_init();
  printf("mgclient version: %s\n", mg_client_version());

  // The seed coordinator, plus the connection template (auth, SSL, ...) that
  // the router reuses for every routed connection it opens.
  mg_session_params *params = mg_session_params_make();
  mg_session_params_set_host(params, argv[1]);
  mg_session_params_set_port(params, (uint16_t)atoi(argv[2]));
  mg_session_params_set_sslmode(params, MG_SSLMODE_DISABLE);
  if (argc >= 4) {
    mg_session_params_set_username(params, argv[3]);
  }
  if (argc >= 5) {
    mg_session_params_set_password(params, argv[4]);
  }

  mg_router_config *config = mg_router_config_make();
  mg_router_config_set_session_params(config, params);
  // The managed-transaction retry budget. These are the defaults, shown here
  // only to make them explicit: up to 8 attempts, backing off 1s, 2s, 4s, ...
  // capped at 15s between them.
  mg_router_config_set_max_retries(config, 8);
  mg_router_config_set_retry_backoff(config, 1.0, 15.0);
  // If the cluster advertises addresses the client can't reach directly, set an
  // address resolver here with mg_router_config_set_resolver(). Omitted for
  // simplicity: advertised addresses are used as-is.

  mg_router *router = mg_router_make(config);
  mg_session_params_destroy(params);
  mg_router_config_destroy(config);
  if (!router) {
    fprintf(stderr, "failed to create router\n");
    mg_finalize();
    return 1;
  }

  int exit_code = 0;

  // Show the topology the coordinator reports.
  print_routing_table(router);

  // Managed write -> routed to the main, wrapped in a transaction, and retried
  // (with a routing refresh) across a failover.
  if (mg_router_execute_write(router, write_greeting, NULL) != 0) {
    printf("write failed: %s\n", mg_router_error(router));
    exit_code = 1;
    goto cleanup;
  }
  printf("wrote greeting to the main\n");

  // Managed read -> routed to a replica (round-robined across replicas on
  // successive reads). Note: under ASYNC replication a replica may momentarily
  // lag the main, so a freshly written value might not be visible yet.
  char greeting[256] = "";
  if (mg_router_execute_read(router, read_greeting, greeting) != 0) {
    printf("read failed: %s\n", mg_router_error(router));
    exit_code = 1;
    goto cleanup;
  }
  printf("read greeting from a replica: \"%s\"\n", greeting);

cleanup:
  mg_router_destroy(router);
  mg_finalize();
  return exit_code;
}
