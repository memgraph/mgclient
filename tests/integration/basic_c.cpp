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

#include <gtest/gtest.h>

#include "mgclient.h"

#include "gmock_wrapper.h"

template <typename T>
T GetEnvOrDefault(const std::string &value_name, const T &default_value) {
  const char *char_value = std::getenv(value_name.c_str());
  if (!char_value) return default_value;
  T value;
  std::stringstream env_value_stream(char_value);
  env_value_stream >> value;
  return value;
}

void CheckMgValueType(const mg_value *value, const mg_value_type value_type) {
  ASSERT_EQ(mg_value_get_type(value), value_type);
}

bool GetBoolValue(const mg_value *value) {
  CheckMgValueType(value, MG_VALUE_TYPE_BOOL);
  return mg_value_bool(value);
}

int GetIntegerValue(const mg_value *value) {
  CheckMgValueType(value, MG_VALUE_TYPE_INTEGER);
  return mg_value_integer(value);
}

double GetFloatValue(const mg_value *value) {
  CheckMgValueType(value, MG_VALUE_TYPE_FLOAT);
  return mg_value_float(value);
}

const mg_node *GetNodeValue(const mg_value *value) {
  CheckMgValueType(value, MG_VALUE_TYPE_NODE);
  return mg_value_node(value);
}

const mg_relationship *GetRelationshipValue(const mg_value *value) {
  CheckMgValueType(value, MG_VALUE_TYPE_RELATIONSHIP);
  return mg_value_relationship(value);
}

std::string GetStringValue(const mg_string *value) {
  return std::string(mg_string_data(value), mg_string_size(value));
}

std::string GetStringValue(const mg_value *value) {
  CheckMgValueType(value, MG_VALUE_TYPE_STRING);
  const mg_string *s = mg_value_string(value);
  return GetStringValue(s);
}

const mg_list *GetListValue(const mg_value *value) {
  CheckMgValueType(value, MG_VALUE_TYPE_LIST);
  return mg_value_list(value);
}

const mg_map *GetMapValue(const mg_value *value) {
  CheckMgValueType(value, MG_VALUE_TYPE_MAP);
  return mg_value_map(value);
}

class MemgraphConnection : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    mg_init();
    params = mg_session_params_make();
    std::string memgraph_host =
        GetEnvOrDefault<std::string>("MEMGRAPH_HOST", "127.0.0.1");
    int memgraph_port = GetEnvOrDefault<int>("MEMGRAPH_PORT", 7687);
    bool memgraph_ssl = GetEnvOrDefault<bool>("MEMGRAPH_SSLMODE", false);

    mg_session_params_set_host(params, memgraph_host.c_str());
    mg_session_params_set_port(params, memgraph_port);
    mg_session_params_set_sslmode(
        params, memgraph_ssl ? MG_SSLMODE_REQUIRE : MG_SSLMODE_DISABLE);
    ASSERT_EQ(mg_connect(params, &session), 0);
    DatabaseCleanup();
  }

  virtual void TearDown() override {
    DatabaseCleanup();
    mg_session_params_destroy(params);
    if (session) {
      mg_session_destroy(session);
    }
    mg_finalize();
  }

  mg_session_params *params;
  mg_session *session;

  void DatabaseCleanup() {
    mg_result *result;
    const char *delete_all_query = "MATCH (n) DETACH DELETE n";

    ASSERT_EQ(mg_session_run(session, delete_all_query, nullptr, nullptr,
                             nullptr, nullptr),
              0);
    ASSERT_EQ(mg_session_pull(session, nullptr), 0);
    ASSERT_EQ(mg_session_fetch(session, &result), 0);
    ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);
  }
};

TEST_F(MemgraphConnection, InsertAndRetriveFromMemegraph) {
  ASSERT_EQ(mg_session_begin_transaction(session, nullptr), 0);
  mg_result *result;
  int status = 0, rows = 0;
  const char *create_query =
      "CREATE (n: TestLabel{id: 1, name: 'test1', is_deleted: true}) "
      "CREATE (n)-[:TestRel {attr: 'attr1'}]->(: TestLabel{id: 12, name: "
      "'test2', is_deleted: false})";
  const char *get_query = "MATCH (n)-[r]->(m) RETURN n, r, m";

  ASSERT_EQ(
      mg_session_run(session, create_query, nullptr, nullptr, nullptr, nullptr),
      0);
  ASSERT_EQ(mg_session_pull(session, nullptr), 0);
  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);

  ASSERT_EQ(
      mg_session_run(session, get_query, nullptr, nullptr, nullptr, nullptr),
      0);
  ASSERT_EQ(mg_session_pull(session, nullptr), 0);
  while ((status = mg_session_fetch(session, &result)) == 1) {
    const mg_list *mg_columns = mg_result_columns(result);
    const mg_list *mg_row = mg_result_row(result);

    ASSERT_EQ(mg_list_size(mg_columns), 3u);
    EXPECT_EQ(GetStringValue(mg_list_at(mg_columns, 0)), "n");
    EXPECT_EQ(GetStringValue(mg_list_at(mg_columns, 1)), "r");
    EXPECT_EQ(GetStringValue(mg_list_at(mg_columns, 2)), "m");

    ASSERT_EQ(mg_list_size(mg_row), 3u);
    const mg_node *node_n = GetNodeValue(mg_list_at(mg_row, 0));
    const mg_node *node_m = GetNodeValue(mg_list_at(mg_row, 2));
    const mg_relationship *relationship_r =
        GetRelationshipValue(mg_list_at(mg_row, 1));

    // Assert Labels
    ASSERT_EQ(mg_node_label_count(node_n), 1u);
    EXPECT_EQ(GetStringValue(mg_node_label_at(node_n, 0)), "TestLabel");
    EXPECT_EQ(GetStringValue(mg_relationship_type(relationship_r)), "TestRel");
    ASSERT_EQ(mg_node_label_count(node_m), 1u);
    EXPECT_EQ(GetStringValue(mg_node_label_at(node_m, 0)), "TestLabel");

    // Assert properties of Node n
    const mg_map *properties_n = mg_node_properties(node_n);
    ASSERT_EQ(mg_map_size(properties_n), 3u);
    EXPECT_EQ(GetIntegerValue(mg_map_at(properties_n, "id")), 1);
    EXPECT_EQ(GetStringValue(mg_map_at(properties_n, "name")), "test1");
    EXPECT_EQ(GetBoolValue(mg_map_at(properties_n, "is_deleted")), 1);

    // Assert properties of Node m
    const mg_map *properties_m = mg_node_properties(node_m);
    ASSERT_EQ(mg_map_size(properties_m), 3u);
    EXPECT_EQ(GetIntegerValue(mg_map_at(properties_m, "id")), 12);
    EXPECT_EQ(GetStringValue(mg_map_at(properties_m, "name")), "test2");
    EXPECT_EQ(GetBoolValue(mg_map_at(properties_m, "is_deleted")), 0);

    // Assert properties of Relationship r
    const mg_map *properties_r = mg_relationship_properties(relationship_r);
    ASSERT_EQ(mg_map_size(properties_r), 1u);
    EXPECT_EQ(GetStringValue(mg_map_at(properties_r, "attr")), "attr1");

    rows++;
  }
  ASSERT_EQ(rows, 1);
  ASSERT_EQ(status, 0);
  ASSERT_EQ(mg_session_commit_transaction(session, &result), 0);

  // The following test is more suitable to be under unit tests. Since it is
  // impossible to execute all unit tests on all platforms (they are not ported
  // yet), the test is located under integration tests.
  {
    ASSERT_EQ(mg_session_run(session,
                             "CREATE (n:ValuesTest {int: 1, string:'Name', "
                             "float: 2.3, bool: True, "
                             "list: [1, 2], map: {key: 'value'}}) RETURN n;",
                             nullptr, nullptr, nullptr, nullptr),
              0);
    ASSERT_EQ(mg_session_pull(session, nullptr), 0);
    mg_result *result;
    ASSERT_EQ(mg_session_fetch(session, &result), 1);
    const mg_list *mg_columns = mg_result_columns(result);
    const mg_list *mg_row = mg_result_row(result);
    ASSERT_EQ(mg_list_size(mg_columns), 1u);
    ASSERT_EQ(mg_list_size(mg_row), 1u);
    const mg_node *node = GetNodeValue(mg_list_at(mg_row, 0));
    const mg_map *properties = mg_node_properties(node);
    EXPECT_EQ(GetIntegerValue(mg_map_at(properties, "int")), 1);
    EXPECT_EQ(GetStringValue(mg_map_at(properties, "string")), "Name");
    EXPECT_LT(std::abs(GetFloatValue(mg_map_at(properties, "float")) - 2.3),
              0.000001);
    EXPECT_EQ(GetBoolValue(mg_map_at(properties, "bool")), true);
    const mg_list *list_value = GetListValue(mg_map_at(properties, "list"));
    ASSERT_EQ(mg_list_size(list_value), 2u);
    ASSERT_EQ(GetIntegerValue(mg_list_at(list_value, 0)), 1);
    ASSERT_EQ(GetIntegerValue(mg_list_at(list_value, 1)), 2);
    const mg_map *map_value = GetMapValue(mg_map_at(properties, "map"));
    ASSERT_EQ(mg_map_size(map_value), 1u);
    ASSERT_EQ(GetStringValue(mg_map_at(map_value, "key")), "value");
    ASSERT_EQ(mg_session_fetch(session, &result), 0);
  }
}
