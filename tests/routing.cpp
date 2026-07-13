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

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mgclient.h"
#include "mgrouting.h"  // internal selection helper (white-box test)

// A resolver (C linkage) that collapses every advertised address to one target,
// used to check resolver application + de-duplication.
extern "C" int CollapseResolver(const char *advertised,
                                mg_resolver_result *result, void *data) {
  (void)advertised;
  (void)data;
  return mg_resolver_result_add(result, "10.0.0.1:7687");
}

// A unit of work (C linkage) that just counts how many times it is invoked, so
// tests can assert whether the router ever reached the work stage.
extern "C" int CountingWork(mg_session *session, void *data) {
  (void)session;
  ++*static_cast<int *>(data);
  return 0;
}

// Drains any pending result stream on `session`. Returns 0 on success, or the
// negative status if a fetch failed.
static int DrainResults(mg_session *session) {
  mg_result *result = nullptr;
  int fetch;
  while ((fetch = mg_session_fetch(session, &result)) == 1) {
  }
  return fetch < 0 ? fetch : 0;
}

// A write unit of work: creates and immediately deletes a node (a net no-op
// that still exercises the write path and commit). Must not commit itself --
// mg_router_execute_write owns the transaction boundary.
extern "C" int WriteNoOpWork(mg_session *session, void *data) {
  (void)data;
  int status =
      mg_session_run(session, "CREATE (n:_MgRouterExecuteTest) DELETE n",
                     nullptr, nullptr, nullptr, nullptr);
  if (status != 0) {
    return status;
  }
  if ((status = mg_session_pull(session, nullptr)) != 0) {
    return status;
  }
  return DrainResults(session);
}

// A read unit of work: runs "RETURN 1" and stores the value through `data`.
extern "C" int ReadReturnsOneWork(mg_session *session, void *data) {
  int status =
      mg_session_run(session, "RETURN 1", nullptr, nullptr, nullptr, nullptr);
  if (status != 0) {
    return status;
  }
  if ((status = mg_session_pull(session, nullptr)) != 0) {
    return status;
  }
  mg_result *result = nullptr;
  int fetch;
  while ((fetch = mg_session_fetch(session, &result)) == 1) {
    const mg_list *row = mg_result_row(result);
    if (row && mg_list_size(row) > 0) {
      const mg_value *value = mg_list_at(row, 0);
      if (value && mg_value_get_type(value) == MG_VALUE_TYPE_INTEGER) {
        *static_cast<int *>(data) = static_cast<int>(mg_value_integer(value));
      }
    }
  }
  return fetch < 0 ? fetch : 0;
}

namespace {

// Initialises mgclient once for the whole test binary (mg_init sets up the
// process-global state that mg_session_pull and friends rely on).
class MgclientEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { mg_init(); }
  void TearDown() override { mg_finalize(); }
};
::testing::Environment *const kMgclientEnv =
    ::testing::AddGlobalTestEnvironment(new MgclientEnvironment);

// Build a `mg_value` list-of-strings from the given addresses.
mg_value *StringList(const std::vector<const char *> &addrs) {
  mg_list *list = mg_list_make_empty(static_cast<uint32_t>(addrs.size()));
  for (const char *addr : addrs) {
    mg_list_append(list, mg_value_make_string(addr));
  }
  return mg_value_make_list(list);
}

// Build one `{"addresses": [...], "role": role}` server map value.
mg_value *Server(const std::vector<const char *> &addrs, const char *role) {
  mg_map *server = mg_map_make_empty(2);
  mg_map_insert(server, "addresses", StringList(addrs));
  mg_map_insert(server, "role", mg_value_make_string(role));
  return mg_value_make_map(server);
}

}  // namespace

TEST(RoutingTable, ParseGroupsAddressesByRole) {
  mg_list *servers = mg_list_make_empty(4);
  mg_list_append(servers, Server({"m:7687"}, "WRITE"));
  mg_list_append(servers, Server({"r1:7687", "r2:7687"}, "READ"));
  mg_list_append(servers, Server({"c1:7687", "c2:7687"}, "ROUTE"));
  // A server with an unrecognised role must be ignored.
  mg_list_append(servers, Server({"x:7687"}, "SOMETHING_ELSE"));

  mg_map *raw = mg_map_make_empty(2);
  mg_map_insert(raw, "ttl", mg_value_make_integer(120));
  mg_map_insert(raw, "servers", mg_value_make_list(servers));

  mg_routing_table *table = mg_routing_table_parse(raw);
  ASSERT_NE(table, nullptr);

  EXPECT_EQ(mg_routing_table_ttl(table), 120);

  ASSERT_EQ(mg_routing_table_address_count(table, MG_ROUTING_ROLE_WRITE), 1u);
  EXPECT_STREQ(mg_routing_table_address_at(table, MG_ROUTING_ROLE_WRITE, 0),
               "m:7687");

  ASSERT_EQ(mg_routing_table_address_count(table, MG_ROUTING_ROLE_READ), 2u);
  EXPECT_STREQ(mg_routing_table_address_at(table, MG_ROUTING_ROLE_READ, 0),
               "r1:7687");
  EXPECT_STREQ(mg_routing_table_address_at(table, MG_ROUTING_ROLE_READ, 1),
               "r2:7687");

  ASSERT_EQ(mg_routing_table_address_count(table, MG_ROUTING_ROLE_ROUTE), 2u);
  EXPECT_STREQ(mg_routing_table_address_at(table, MG_ROUTING_ROLE_ROUTE, 0),
               "c1:7687");
  EXPECT_STREQ(mg_routing_table_address_at(table, MG_ROUTING_ROLE_ROUTE, 1),
               "c2:7687");

  mg_routing_table_destroy(table);
  mg_map_destroy(raw);
}

TEST(RoutingTable, ParseNullReturnsNull) {
  EXPECT_EQ(mg_routing_table_parse(nullptr), nullptr);
}

TEST(ErrorClassification, TransientCoversServerAndTransportFailures) {
  // Server-signalled transient + low-level transport/connection failures.
  EXPECT_TRUE(mg_error_is_transient(MG_ERROR_TRANSIENT_ERROR));
  EXPECT_TRUE(mg_error_is_transient(MG_ERROR_SEND_FAILED));
  EXPECT_TRUE(mg_error_is_transient(MG_ERROR_RECV_FAILED));
  EXPECT_TRUE(mg_error_is_transient(MG_ERROR_NETWORK_FAILURE));
  EXPECT_TRUE(mg_error_is_transient(MG_ERROR_SOCKET));
}

TEST(ErrorClassification, TransientIsFalseForNonTransportFailures) {
  EXPECT_FALSE(mg_error_is_transient(0));  // success
  EXPECT_FALSE(mg_error_is_transient(MG_ERROR_CLIENT_ERROR));
  EXPECT_FALSE(mg_error_is_transient(MG_ERROR_DATABASE_ERROR));
  EXPECT_FALSE(mg_error_is_transient(MG_ERROR_BAD_PARAMETER));
  EXPECT_FALSE(mg_error_is_transient(MG_ERROR_DECODING_FAILED));
  EXPECT_FALSE(mg_error_is_transient(MG_ERROR_PROTOCOL_VIOLATION));
  EXPECT_FALSE(mg_error_is_transient(MG_ERROR_SSL_ERROR));
}

// ---------------------------------------------------------------------------
// Router selection (round-robin) -- pure logic via the internal seam.
// ---------------------------------------------------------------------------

namespace {
mg_routing_table *ParseTable(int64_t ttl, mg_list *servers) {
  mg_map *raw = mg_map_make_empty(2);
  mg_map_insert(raw, "ttl", mg_value_make_integer(ttl));
  mg_map_insert(raw, "servers", mg_value_make_list(servers));
  mg_routing_table *table = mg_routing_table_parse(raw);
  mg_map_destroy(raw);
  return table;
}
}  // namespace

TEST(RouterSelect, ReadRoundRobinsAcrossReplicas) {
  mg_list *servers = mg_list_make_empty(2);
  mg_list_append(servers, Server({"m:7687"}, "WRITE"));
  mg_list_append(servers, Server({"r1:7687", "r2:7687", "r3:7687"}, "READ"));
  mg_routing_table *table = ParseTable(300, servers);
  ASSERT_NE(table, nullptr);

  uint32_t read_index = 0;
  std::vector<std::string> firsts;
  for (int i = 0; i < 4; ++i) {
    mg_addr_list out;
    memset(&out, 0, sizeof(out));
    ASSERT_EQ(mg_routing_select_targets(table, MG_ROUTING_ROLE_READ, nullptr,
                                        nullptr, &read_index, &out),
              0);
    ASSERT_EQ(out.size, 3u);  // every replica stays a failover candidate
    firsts.emplace_back(out.items[0]);
    mg_addr_list_clear(&out);
  }
  // Each selection starts at the next replica, wrapping around.
  EXPECT_EQ(firsts[0], "r1:7687");
  EXPECT_EQ(firsts[1], "r2:7687");
  EXPECT_EQ(firsts[2], "r3:7687");
  EXPECT_EQ(firsts[3], "r1:7687");

  mg_routing_table_destroy(table);
}

TEST(RouterSelect, WriteDoesNotRotate) {
  mg_list *servers = mg_list_make_empty(2);
  mg_list_append(servers, Server({"m:7687"}, "WRITE"));
  mg_list_append(servers, Server({"r1:7687", "r2:7687"}, "READ"));
  mg_routing_table *table = ParseTable(300, servers);
  ASSERT_NE(table, nullptr);

  uint32_t read_index = 5;
  mg_addr_list out;
  memset(&out, 0, sizeof(out));
  ASSERT_EQ(mg_routing_select_targets(table, MG_ROUTING_ROLE_WRITE, nullptr,
                                      nullptr, &read_index, &out),
            0);
  ASSERT_EQ(out.size, 1u);
  EXPECT_STREQ(out.items[0], "m:7687");
  EXPECT_EQ(read_index, 5u);  // WRITE selection leaves the READ cursor alone
  mg_addr_list_clear(&out);

  mg_routing_table_destroy(table);
}

TEST(RouterSelect, ResolverAppliedAndDuplicatesSkipped) {
  mg_list *servers = mg_list_make_empty(1);
  mg_list_append(servers, Server({"r1:7687", "r2:7687"}, "READ"));
  mg_routing_table *table = ParseTable(300, servers);
  ASSERT_NE(table, nullptr);

  uint32_t read_index = 0;
  mg_addr_list out;
  memset(&out, 0, sizeof(out));
  ASSERT_EQ(
      mg_routing_select_targets(table, MG_ROUTING_ROLE_READ, CollapseResolver,
                                nullptr, &read_index, &out),
      0);
  // Both replicas resolve to the same target, so it is listed once.
  ASSERT_EQ(out.size, 1u);
  EXPECT_STREQ(out.items[0], "10.0.0.1:7687");
  mg_addr_list_clear(&out);

  mg_routing_table_destroy(table);
}

// ---------------------------------------------------------------------------
// Router config + lifecycle (no cluster needed).
// ---------------------------------------------------------------------------

namespace {
mg_session_params *SeedParams(const char *host, uint16_t port) {
  mg_session_params *params = mg_session_params_make();
  mg_session_params_set_host(params, host);
  mg_session_params_set_port(params, port);
  return params;
}
}  // namespace

TEST(RouterConfig, MakeAndDestroy) {
  mg_router_config *config = mg_router_config_make();
  ASSERT_NE(config, nullptr);
  mg_router_config_destroy(config);
  mg_router_config_destroy(nullptr);  // must be a no-op
}

TEST(Router, MakeRequiresConfigAndSessionParams) {
  EXPECT_EQ(mg_router_make(nullptr), nullptr);

  // A config with no session params set cannot make a router.
  mg_router_config *config = mg_router_config_make();
  EXPECT_EQ(mg_router_make(config), nullptr);
  mg_router_config_destroy(config);
}

TEST(Router, MakeCopiesConfigSoItCanBeFreed) {
  mg_router_config *config = mg_router_config_make();
  mg_session_params *params = SeedParams("coordinator", 7687);
  mg_session_params_set_username(params, "user");
  mg_session_params_set_password(params, "pass");
  mg_router_config_set_session_params(config, params);

  mg_router *router = mg_router_make(config);
  ASSERT_NE(router, nullptr);

  // The router copied what it needs, so the params and config may be freed
  // now while the router lives on (ASan verifies the deep copy is clean).
  mg_session_params_destroy(params);
  mg_router_config_destroy(config);

  mg_router_destroy(router);
  mg_router_destroy(nullptr);  // must be a no-op
}

namespace {
mg_router *MakeRouter(const char *host, uint16_t port) {
  mg_router_config *config = mg_router_config_make();
  mg_session_params *params = SeedParams(host, port);
  mg_router_config_set_session_params(config, params);
  mg_router *router = mg_router_make(config);
  mg_session_params_destroy(params);
  mg_router_config_destroy(config);
  return router;
}

// Returns the coordinator port from MEMGRAPH_HA_COORDINATOR_PORT (default
// 7687).
uint16_t CoordinatorPort() {
  const char *port_str = std::getenv("MEMGRAPH_HA_COORDINATOR_PORT");
  return port_str ? static_cast<uint16_t>(std::atoi(port_str)) : 7687;
}
}  // namespace

TEST(RouterRefresh, RejectsNullRouter) {
  EXPECT_EQ(mg_router_refresh(nullptr), MG_ERROR_BAD_PARAMETER);
}

TEST(RouterRefresh, AccessorsBeforeAnyRefresh) {
  mg_router *router = MakeRouter("127.0.0.1", 7687);
  ASSERT_NE(router, nullptr);
  EXPECT_EQ(mg_router_routing_table(router), nullptr);
  EXPECT_STREQ(mg_router_error(router), "");
  mg_router_destroy(router);
}

TEST(RouterRefresh, FailsWhenSeedUnreachable) {
  // Port 1 has nothing listening, so the refresh can't reach any coordinator.
  mg_router *router = MakeRouter("127.0.0.1", 1);
  ASSERT_NE(router, nullptr);

  int status = mg_router_refresh(router);
  EXPECT_NE(status, 0);
  EXPECT_TRUE(
      mg_error_is_transient(status));         // connection refused is transient
  EXPECT_STRNE(mg_router_error(router), "");  // a message was recorded
  EXPECT_EQ(mg_router_routing_table(router), nullptr);  // nothing cached

  mg_router_destroy(router);
}

TEST(RouterConnect, UnreachableSeedKeepsRefreshError) {
  // With no cached table and an unreachable seed, connecting must surface the
  // refresh failure (e.g. the coordinator connection error), not overwrite it
  // with a misleading "no <role> server in the routing table".
  mg_router *router = MakeRouter("127.0.0.1", 1);
  ASSERT_NE(router, nullptr);

  ASSERT_NE(mg_router_refresh(router), 0);
  const std::string refresh_error = mg_router_error(router);
  ASSERT_FALSE(refresh_error.empty());

  mg_session *session = nullptr;
  int status = mg_router_connect_write(router, &session);
  EXPECT_NE(status, 0);
  EXPECT_EQ(session, nullptr);
  // The connect surfaces the same coordinator-connection failure, not a
  // fabricated routing-table message.
  EXPECT_EQ(std::string(mg_router_error(router)), refresh_error);

  mg_router_destroy(router);
}

// Runs only against a real HA cluster; set MEMGRAPH_HA_COORDINATOR_HOST (and
// optionally _PORT) to enable it.
TEST(RouterRefresh, FetchesTableFromCoordinator) {
  const char *host = std::getenv("MEMGRAPH_HA_COORDINATOR_HOST");
  if (!host) {
    GTEST_SKIP() << "set MEMGRAPH_HA_COORDINATOR_HOST to run";
  }

  mg_router *router = MakeRouter(host, CoordinatorPort());
  ASSERT_NE(router, nullptr);

  int status = mg_router_refresh(router);
  ASSERT_EQ(status, 0) << mg_router_error(router);

  const mg_routing_table *table = mg_router_routing_table(router);
  ASSERT_NE(table, nullptr);
  EXPECT_GT(mg_routing_table_ttl(table), 0);
  EXPECT_GT(mg_routing_table_address_count(table, MG_ROUTING_ROLE_WRITE), 0u);
  EXPECT_GT(mg_routing_table_address_count(table, MG_ROUTING_ROLE_READ), 0u);
  EXPECT_GT(mg_routing_table_address_count(table, MG_ROUTING_ROLE_ROUTE), 0u);

  mg_router_destroy(router);
}

namespace {
std::string ReplicationRole(mg_session *session) {
  std::string role;
  if (mg_session_run(session, "SHOW REPLICATION ROLE", nullptr, nullptr,
                     nullptr, nullptr) != 0 ||
      mg_session_pull(session, nullptr) != 0) {
    return role;
  }
  mg_result *result = nullptr;
  while (mg_session_fetch(session, &result) == 1) {
    const mg_list *row = mg_result_row(result);
    if (row && mg_list_size(row) > 0) {
      const mg_value *value = mg_list_at(row, 0);
      if (value && mg_value_get_type(value) == MG_VALUE_TYPE_STRING) {
        const mg_string *str = mg_value_string(value);
        role.assign(mg_string_data(str), mg_string_size(str));
      }
    }
  }
  return role;
}
}  // namespace

// Cluster-gated: set MEMGRAPH_HA_COORDINATOR_HOST to run these. The cluster's
// advertised instance addresses must be directly reachable from here (e.g. a
// local Docker HA cluster on the host network, as CI runs).
TEST(RouterConnect, WriteReachesMain) {
  const char *host = std::getenv("MEMGRAPH_HA_COORDINATOR_HOST");
  if (!host) {
    GTEST_SKIP() << "set MEMGRAPH_HA_COORDINATOR_HOST to run";
  }
  mg_router *router = MakeRouter(host, CoordinatorPort());
  ASSERT_NE(router, nullptr);

  mg_session *session = nullptr;
  int status = mg_router_connect_write(router, &session);
  ASSERT_EQ(status, 0) << mg_router_error(router);
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(ReplicationRole(session), "main");

  mg_session_destroy(session);
  mg_router_destroy(router);
}

TEST(RouterConnect, ReadReachesReplica) {
  const char *host = std::getenv("MEMGRAPH_HA_COORDINATOR_HOST");
  if (!host) {
    GTEST_SKIP() << "set MEMGRAPH_HA_COORDINATOR_HOST to run";
  }
  mg_router *router = MakeRouter(host, CoordinatorPort());
  ASSERT_NE(router, nullptr);

  mg_session *session = nullptr;
  int status = mg_router_connect_read(router, &session);
  ASSERT_EQ(status, 0) << mg_router_error(router);
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(ReplicationRole(session), "replica");

  mg_session_destroy(session);
  mg_router_destroy(router);
}

TEST(RoutingTable, AddressAtOutOfRangeReturnsNull) {
  mg_list *servers = mg_list_make_empty(1);
  mg_list_append(servers, Server({"m:7687"}, "WRITE"));
  mg_map *raw = mg_map_make_empty(2);
  mg_map_insert(raw, "ttl", mg_value_make_integer(1));
  mg_map_insert(raw, "servers", mg_value_make_list(servers));

  mg_routing_table *table = mg_routing_table_parse(raw);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(mg_routing_table_address_at(table, MG_ROUTING_ROLE_WRITE, 5),
            nullptr);
  EXPECT_EQ(mg_routing_table_address_count(table, MG_ROUTING_ROLE_READ), 0u);

  mg_routing_table_destroy(table);
  mg_map_destroy(raw);
}

TEST(RoutingTable, ParseIgnoresMalformedEntries) {
  // A non-integer "ttl" defaults to 0; a server that is not a map, or whose
  // "role"/"addresses" have the wrong type, is skipped -- none of this fails
  // the parse.
  mg_list *servers = mg_list_make_empty(4);
  mg_list_append(servers, mg_value_make_integer(42));  // not a map
  {
    mg_map *no_role = mg_map_make_empty(1);
    mg_map_insert(no_role, "addresses", StringList({"a:7687"}));
    mg_list_append(servers, mg_value_make_map(no_role));  // missing "role"
  }
  {
    mg_map *bad_addrs = mg_map_make_empty(2);
    mg_map_insert(bad_addrs, "role", mg_value_make_string("WRITE"));
    mg_map_insert(bad_addrs, "addresses",
                  mg_value_make_integer(1));  // not a list
    mg_list_append(servers, mg_value_make_map(bad_addrs));
  }
  // A valid server survives alongside the malformed ones.
  mg_list_append(servers, Server({"m:7687"}, "WRITE"));

  mg_map *raw = mg_map_make_empty(2);
  mg_map_insert(raw, "ttl", mg_value_make_string("not-an-integer"));
  mg_map_insert(raw, "servers", mg_value_make_list(servers));

  mg_routing_table *table = mg_routing_table_parse(raw);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(mg_routing_table_ttl(table), 0);  // non-integer ttl -> 0
  ASSERT_EQ(mg_routing_table_address_count(table, MG_ROUTING_ROLE_WRITE), 1u);
  EXPECT_STREQ(mg_routing_table_address_at(table, MG_ROUTING_ROLE_WRITE, 0),
               "m:7687");

  mg_routing_table_destroy(table);
  mg_map_destroy(raw);
}

// ---------------------------------------------------------------------------
// Managed transactions: retry policy (pure) + execute lifecycle.
// ---------------------------------------------------------------------------

TEST(RouterBackoff, CappedExponential) {
  // base=1, cap=15: 1, 2, 4, 8, then capped at 15.
  EXPECT_DOUBLE_EQ(mg_router_backoff_seconds(1, 1.0, 15.0), 1.0);
  EXPECT_DOUBLE_EQ(mg_router_backoff_seconds(2, 1.0, 15.0), 2.0);
  EXPECT_DOUBLE_EQ(mg_router_backoff_seconds(3, 1.0, 15.0), 4.0);
  EXPECT_DOUBLE_EQ(mg_router_backoff_seconds(4, 1.0, 15.0), 8.0);
  EXPECT_DOUBLE_EQ(mg_router_backoff_seconds(5, 1.0, 15.0), 15.0);  // capped
  EXPECT_DOUBLE_EQ(mg_router_backoff_seconds(9, 1.0, 15.0),
                   15.0);  // stays capped
}

TEST(RouterBackoff, EdgeCases) {
  EXPECT_DOUBLE_EQ(mg_router_backoff_seconds(0, 1.0, 15.0),
                   0.0);  // no attempt 0
  EXPECT_DOUBLE_EQ(mg_router_backoff_seconds(1, 0.0, 15.0), 0.0);  // zero base
  EXPECT_DOUBLE_EQ(mg_router_backoff_seconds(3, 2.5, 5.0),
                   5.0);  // 2.5,5,capped
}

namespace {
mg_router *MakeRouterWithRetries(const char *host, uint16_t port,
                                 uint32_t max_retries, double base,
                                 double cap) {
  mg_router_config *config = mg_router_config_make();
  mg_session_params *params = SeedParams(host, port);
  mg_router_config_set_session_params(config, params);
  mg_router_config_set_max_retries(config, max_retries);
  mg_router_config_set_retry_backoff(config, base, cap);
  mg_router *router = mg_router_make(config);
  mg_session_params_destroy(params);
  mg_router_config_destroy(config);
  return router;
}
}  // namespace

TEST(RouterExecute, FailsWhenSeedUnreachable) {
  // Nothing listens on port 1, so no READ connection can be established; the
  // work must never run, and the transient failure is reported after retries.
  // base=cap=0 keeps the test fast (no real sleeping between attempts).
  mg_router *router = MakeRouterWithRetries("127.0.0.1", 1, 2, 0.0, 0.0);
  ASSERT_NE(router, nullptr);

  int calls = 0;
  int status = mg_router_execute_read(router, CountingWork, &calls);
  EXPECT_NE(status, 0);
  EXPECT_TRUE(mg_error_is_transient(status));
  EXPECT_EQ(calls, 0);  // connect never succeeded, so work never ran
  EXPECT_STRNE(mg_router_error(router), "");

  mg_router_destroy(router);
}

TEST(RouterExecute, RejectsNullWork) {
  mg_router *router = MakeRouter("127.0.0.1", 7687);
  ASSERT_NE(router, nullptr);
  EXPECT_EQ(mg_router_execute_read(router, nullptr, nullptr),
            MG_ERROR_BAD_PARAMETER);
  EXPECT_EQ(mg_router_execute_write(router, nullptr, nullptr),
            MG_ERROR_BAD_PARAMETER);
  mg_router_destroy(router);
}

// Cluster-gated (see the RouterConnect tests above for the env vars).
TEST(RouterExecute, WriteCommits) {
  const char *host = std::getenv("MEMGRAPH_HA_COORDINATOR_HOST");
  if (!host) {
    GTEST_SKIP() << "set MEMGRAPH_HA_COORDINATOR_HOST to run";
  }
  mg_router *router = MakeRouter(host, CoordinatorPort());
  ASSERT_NE(router, nullptr);

  int status = mg_router_execute_write(router, WriteNoOpWork, nullptr);
  EXPECT_EQ(status, 0) << mg_router_error(router);

  mg_router_destroy(router);
}

TEST(RouterExecute, ReadRuns) {
  const char *host = std::getenv("MEMGRAPH_HA_COORDINATOR_HOST");
  if (!host) {
    GTEST_SKIP() << "set MEMGRAPH_HA_COORDINATOR_HOST to run";
  }
  mg_router *router = MakeRouter(host, CoordinatorPort());
  ASSERT_NE(router, nullptr);

  int value = 0;
  int status = mg_router_execute_read(router, ReadReturnsOneWork, &value);
  ASSERT_EQ(status, 0) << mg_router_error(router);
  EXPECT_EQ(value, 1);

  mg_router_destroy(router);
}

TEST(RoutingTable, ParseIsLenientAboutMissingKeys) {
  // No "ttl" (defaults to 0) and an empty "servers" list yield an empty table,
  // not NULL.
  mg_map *raw = mg_map_make_empty(1);
  mg_map_insert(raw, "servers", mg_value_make_list(mg_list_make_empty(0)));

  mg_routing_table *table = mg_routing_table_parse(raw);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(mg_routing_table_ttl(table), 0);
  EXPECT_EQ(mg_routing_table_address_count(table, MG_ROUTING_ROLE_WRITE), 0u);
  EXPECT_EQ(mg_routing_table_address_count(table, MG_ROUTING_ROLE_READ), 0u);
  EXPECT_EQ(mg_routing_table_address_count(table, MG_ROUTING_ROLE_ROUTE), 0u);

  mg_routing_table_destroy(table);
  mg_map_destroy(raw);
}
