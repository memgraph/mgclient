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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mgclient.hpp"

template <typename T>
T GetEnvOrDefault(const std::string &value_name, const T &default_value) {
  const char *char_value = std::getenv(value_name.c_str());
  if (!char_value) return default_value;
  T value;
  std::stringstream env_value_stream(char_value);
  env_value_stream >> value;
  return value;
}

class MemgraphConnection : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    mg::Client::Init();

    client = mg::Client::Connect(
        {GetEnvOrDefault<std::string>("MEMGRAPH_HOST", "127.0.0.1"),
         GetEnvOrDefault<uint16_t>("MEMGRAPH_PORT", 7687), "", "",
         GetEnvOrDefault<bool>("MEMGRAPH_SSLMODE", false), ""});

    ASSERT_TRUE(client);

    // Clean the database for the tests
    ASSERT_TRUE(client->Execute(delete_all_query));
    ASSERT_FALSE(client->FetchOne());
  }

  virtual void TearDown() override {
    // Check if all results are consumed in the tests
    ASSERT_FALSE(client->FetchOne());

    ASSERT_TRUE(client->Execute(delete_all_query));
    ASSERT_FALSE(client->FetchOne());

    // Deallocate the client because mg_finalize has to be called globally.
    client.reset(nullptr);

    mg::Client::Finalize();
  }

  std::unique_ptr<mg::Client> client;
  const char *delete_all_query = "MATCH (n) DETACH DELETE n";
};

TEST_F(MemgraphConnection, InsertAndRetrieveFromMemgraph) {
  ASSERT_NE(client, nullptr);
  ASSERT_TRUE(client->BeginTransaction());

  const auto create_query =
      "CREATE (n: TestLabel{id: 1, name: 'test1', is_deleted: true}) "
      "CREATE (n)-[:TestRel {attr: 'attr1'}]->(: TestLabel{id: 12, name: "
      "'test2', is_deleted: false})";
  ASSERT_TRUE(client->Execute(create_query));
  auto maybe_result = client->FetchOne();
  ASSERT_FALSE(maybe_result);

  const auto get_query = "MATCH (n)-[r]->(m) RETURN n, r, m";
  ASSERT_TRUE(client->Execute(get_query));
  int result_counter = 0;
  while ((maybe_result = client->FetchOne())) {
    const auto &result = *maybe_result;
    ASSERT_EQ(result.size(), 3);

    ASSERT_EQ(result[0].type(), mg::Value::Type::Node);
    const auto node_n = result[0].ValueNode();
    ASSERT_EQ(result[2].type(), mg::Value::Type::Node);
    const auto node_m = result[2].ValueNode();
    ASSERT_EQ(result[1].type(), mg::Value::Type::Relationship);
    const auto relationship_r = result[1].ValueRelationship();

    // Assert labels
    const auto n_labels = node_n.labels();
    ASSERT_EQ(n_labels.size(), 1);
    EXPECT_EQ(n_labels[0], "TestLabel");
    const auto m_labels = node_m.labels();
    ASSERT_EQ(m_labels.size(), 1);
    EXPECT_EQ(m_labels[0], "TestLabel");
    const auto r_type = relationship_r.type();
    EXPECT_EQ(r_type, "TestRel");

    // Assert properties of Node n
    const auto n_properties = node_n.properties();
    ASSERT_EQ(n_properties.size(), 3);
    EXPECT_EQ(n_properties["id"].ValueInt(), 1);
    EXPECT_EQ(n_properties["name"].ValueString(), "test1");
    EXPECT_EQ(n_properties["is_deleted"].ValueBool(), true);

    // Assert properties of Node m
    const auto m_properties = node_m.properties();
    ASSERT_EQ(m_properties.size(), 3);
    EXPECT_EQ(m_properties["id"].ValueInt(), 12);
    EXPECT_EQ(m_properties["name"].ValueString(), "test2");
    EXPECT_EQ(m_properties["is_deleted"].ValueBool(), false);

    // Assert properties of Relationship r
    const auto r_properties = relationship_r.properties();
    ASSERT_EQ(r_properties.size(), 1);
    EXPECT_EQ(r_properties["attr"].ValueString(), "attr1");

    ++result_counter;
  }
  ASSERT_EQ(result_counter, 1);
  ASSERT_TRUE(client->CommitTransaction());

  {
    ASSERT_TRUE(client->Execute(
        "CREATE (n:ValuesTest {int: 1, string:'Name', float: 2.3, bool: True, "
        "list: [1, 2], map: {key: 'value'}}) RETURN n;"));
    auto maybe_result = client->FetchOne();
    ASSERT_TRUE(maybe_result);
    const auto &row = *maybe_result;
    EXPECT_EQ(row.size(), 1);
    const auto node = row[0];
    ASSERT_EQ(node.type(), mg::Value::Type::Node);
    const auto node_props = node.ValueNode().properties();
    ASSERT_EQ(node_props["int"].ValueInt(), 1);
    ASSERT_EQ(node_props["string"].ValueString(), "Name");
    ASSERT_LT(std::abs(node_props["float"].ValueDouble() - 2.3), 0.000001);
    ASSERT_EQ(node_props["bool"].ValueBool(), true);
    ASSERT_EQ(node_props["list"].type(), mg::Value::Type::List);
    const auto list_value = node_props["list"].ValueList();
    ASSERT_EQ(list_value.size(), 2);
    ASSERT_EQ(list_value[0].ValueInt(), 1);
    ASSERT_EQ(list_value[1].ValueInt(), 2);
    ASSERT_EQ(node_props["map"].type(), mg::Value::Type::Map);
    const auto map_value = node_props["map"].ValueMap();
    ASSERT_EQ(map_value.size(), 1);
    ASSERT_EQ(map_value["key"].ValueString(), "value");
    ASSERT_FALSE(client->FetchOne());
  }
}
