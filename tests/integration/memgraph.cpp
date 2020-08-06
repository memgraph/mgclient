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
#include <stdio.h>
#include <stdlib.h>

#include "mgclient.h"
extern "C" {
#include "mgvalue.h"
}

template <typename T>
T GetEnvOrDefault(const std::string &value_name, const T &default_value) {
  const char *char_value = std::getenv(value_name.c_str());
  if (char_value) {
    T value;
    std::stringstream env_value_stream(char_value);
    env_value_stream >> value;
    return value;
  }
  return default_value;
}

std::string GetStringValue(const mg_string *value) {
  return std::string(value->data, value->size);
}

std::string GetStringValue(const mg_value *value) {
  const mg_string *s = mg_value_string(value);
  return GetStringValue(s);
}

class MemgraphConnection : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    params = mg_session_params_make();
    std::string memgraph_host =
        GetEnvOrDefault<std::string>("MEMGRAPH_HOST", "127.0.0.1");
    int memgraph_port = GetEnvOrDefault<int>("MEMGRAPH_PORT", 7687);
    bool memgraph_ssl = GetEnvOrDefault<bool>("MEMGRAPH_SSLMODE", true);

    mg_session_params_set_host(params, memgraph_host.c_str());
    mg_session_params_set_port(params, memgraph_port);
    mg_session_params_set_sslmode(
        params, memgraph_ssl ? MG_SSLMODE_REQUIRE : MG_SSLMODE_DISABLE);
  }

  virtual void TearDown() override {
    mg_result *result;
    mg_session_run(session, "MATCH (n) DETACH DELETE n", NULL, NULL);
    mg_session_pull(session, &result);
    mg_session_params_destroy(params);
    if (session) {
      mg_session_destroy(session);
    }
  }

  mg_session_params *params;
  mg_session *session;
};

TEST_F(MemgraphConnection, CanConnectToMemgraph) {
  int status = mg_connect(params, &session);

  ASSERT_EQ(status, 0);
}

TEST_F(MemgraphConnection, InsertAndRetriveFromMemegraph) {
  mg_result *result;
  int status = 0, rows = 0;
  const char *create_query =
      "\
        CREATE (n: TestLabel{id: 1, name: 'test1', is_deleted: true}) \
        CREATE (n)-[:TestRel {attr: 'attr1'}]->(: TestLabel{id: 12, name: 'test2', is_deleted: false}) \
    ";
  const char *get_query =
      "\
        MATCH (n)-[r]->(m) \
        RETURN n, r, m \
    ";

  mg_connect(params, &session);
  mg_session_run(session, create_query, NULL, NULL);
  mg_session_pull(session, &result);
  mg_session_run(session, get_query, NULL, NULL);

  while ((status = mg_session_pull(session, &result)) == 1) {
    const mg_list *mg_columns = mg_result_columns(result);
    const mg_list *mg_row = mg_result_row(result);

    auto lsa = mg_value_get_type(mg_list_at(mg_columns, 0));
    auto lsaa = GetStringValue(mg_list_at(mg_columns, 0));

    EXPECT_EQ(GetStringValue(mg_list_at(mg_columns, 0)), "n");
    EXPECT_EQ(GetStringValue(mg_list_at(mg_columns, 1)), "r");
    EXPECT_EQ(GetStringValue(mg_list_at(mg_columns, 2)), "m");

    const mg_node *node_n = mg_value_node(mg_row->elements[0]);
    const mg_node *node_m = mg_value_node(mg_row->elements[2]);
    const mg_relationship *relationship_r =
        mg_value_relationship(mg_row->elements[1]);

    EXPECT_EQ(GetStringValue(mg_node_label_at(node_n, 0)), "TestLabel");
    EXPECT_EQ(GetStringValue(mg_relationship_type(relationship_r)), "TestRel");
    EXPECT_EQ(GetStringValue(mg_node_label_at(node_m, 0)), "TestLabel");

    // Assert properties of Node n
    const mg_map *properties_n = mg_node_properties(node_n);
    EXPECT_EQ(mg_value_integer(mg_map_at(properties_n, "id")), 1);
    EXPECT_EQ(GetStringValue(mg_map_at(properties_n, "name")), "test1");
    EXPECT_EQ(mg_value_bool(mg_map_at(properties_n, "is_deleted")), 1);

    // Assert properties of Node m
    const mg_map *properties_m = mg_node_properties(node_m);
    EXPECT_EQ(mg_value_integer(mg_map_at(properties_m, "id")), 12);
    EXPECT_EQ(GetStringValue(mg_map_at(properties_m, "name")), "test2");
    EXPECT_EQ(mg_value_bool(mg_map_at(properties_m, "is_deleted")), 0);

    // Assert properties of Relationship r
    const mg_map *properties_r = mg_relationship_properties(relationship_r);
    EXPECT_EQ(GetStringValue(mg_map_at(properties_r, "attr")), "attr1");

    rows++;
  }
  ASSERT_EQ(rows, 1);
  ASSERT_EQ(status, 0);
}
