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

#include <vector>

#include "mgclient.h"

namespace {

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

TEST(ErrorClassification, CommittedOnMainNeedsBothMarkers) {
  const char *committed =
      "Replication Exception: Failed to replicate to SYNC replica 'instance_1': "
      "replica is not reachable or not in sync with the main. Transaction is "
      "still committed on the main instance and other alive replicas.";
  EXPECT_TRUE(mg_error_is_committed_on_main(committed));

  // Case-insensitive.
  EXPECT_TRUE(mg_error_is_committed_on_main(
      "REPLICATION EXCEPTION ... COMMITTED ON THE MAIN instance"));

  // A replication error where the transaction was aborted is NOT committed.
  EXPECT_FALSE(mg_error_is_committed_on_main(
      "Replication Exception: ... Transaction was aborted on all instances."));
  // Unrelated errors and NULL.
  EXPECT_FALSE(mg_error_is_committed_on_main("Syntax error near 'FOO'"));
  EXPECT_FALSE(mg_error_is_committed_on_main(nullptr));
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
    mg_map_insert(bad_addrs, "addresses", mg_value_make_integer(1));  // not a list
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
