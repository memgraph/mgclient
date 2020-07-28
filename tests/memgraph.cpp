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
T getEnvOrDefault(const std::string &valueName, const T &defaultValue)
{
    const char *charValue = std::getenv(valueName.c_str());
    if (charValue)
    {
        T value;
        std::stringstream envValueStream(charValue);
        envValueStream >> value;
        return value;
    }
    return defaultValue;
}

template <>
std::string getEnvOrDefault<std::string>(const std::string &valueName, const std::string &defaultValue)
{
    const char *value = std::getenv(valueName.c_str());
    return value ? value : defaultValue;
}

class MemgraphConenction : public ::testing::Test
{
protected:
    void SetUp() override
    {
        params = mg_session_params_make();
        std::string memgraphHost = getEnvOrDefault<std::string>("MEMGRAPH_HOST", "127.0.0.1");
        int memgraphPort = getEnvOrDefault<int>("MEMGRAPH_PORT", 7687);
        bool memgraphSSL = getEnvOrDefault<bool>("MEMGRAPH_SSLMODE", true);

        mg_session_params_set_host(params, memgraphHost.c_str());
        mg_session_params_set_port(params, memgraphPort);
        mg_session_params_set_sslmode(params, memgraphSSL ? MG_SSLMODE_REQUIRE : MG_SSLMODE_DISABLE);
    }

    virtual void TearDown()
    {
        mg_result *result;
        mg_session_run(session, "MATCH (n) DETACH DELETE n", NULL, NULL);
        mg_session_pull(session, &result);
        mg_session_params_destroy(params);
        if (session)
        {
            mg_session_destroy(session);
        }
    }

    mg_session_params *params;
    mg_session *session;
};

TEST_F(MemgraphConenction, CanConnectToMemgraph)
{
    int status = mg_connect(params, &session);

    ASSERT_EQ(status, 0);
}

TEST_F(MemgraphConenction, InsertAndRetriveFromMemegraph)
{
    mg_result *result;
    int status = 0, rows = 0;
    const char* createQuery = "\
        CREATE (n: TestLabel{id: 1, name: 'test1', is_deleted: true}) \
        CREATE (n)-[:TestRel {attr: 'attr1'}]->(: TestLabel{id: 12, name: 'test2', is_deleted: false}) \
    ";
    const char* getQuery = "\
        MATCH (n)-[r]->(m) \
        RETURN n, r, m \
    ";

    mg_connect(params, &session);
    mg_session_run(session, createQuery, NULL, NULL);
    mg_session_pull(session, &result);
    mg_session_run(session, getQuery, NULL, NULL);

    while ((status = mg_session_pull(session, &result)) == 1) {
        const mg_list *mgColumns = mg_result_columns(result);
        const mg_list *mgRow = mg_result_row(result);

        ASSERT_EQ(std::string(mg_value_string(mgColumns->elements[0])->data), "none"); // Why none, and not n?
        ASSERT_EQ(std::string(mg_value_string(mgColumns->elements[1])->data), "r");
        ASSERT_EQ(std::string(mg_value_string(mgColumns->elements[2])->data), "m");

        const mg_node *nodeN = mg_value_node(mgRow->elements[0]);
        const mg_node *nodeM = mg_value_node(mgRow->elements[2]);
        const mg_relationship *relationshipR = mg_value_relationship(mgRow->elements[1]);

        ASSERT_STREQ(mg_node_label_at(nodeN, 0)->data, "TestLabelmate"); // Why mate?
        ASSERT_STREQ(mg_relationship_type(relationshipR)->data, "TestRel");
        ASSERT_STREQ(mg_node_label_at(nodeM, 0)->data, "TestLabel");

        // Assert properties of Node n
        const mg_map *propertiesN = mg_node_properties(nodeN);
        ASSERT_EQ(mg_value_integer(mg_map_at(propertiesN, "id")), 1);
        ASSERT_STREQ(mg_value_string(mg_map_at(propertiesN, "name"))->data, "test1");
        ASSERT_EQ(mg_value_bool(mg_map_at(propertiesN, "is_deleted")), 1);

        // Assert properties of Node m
        const mg_map *propertiesM = mg_node_properties(nodeM);
        ASSERT_EQ(mg_value_integer(mg_map_at(propertiesM, "id")), 12);
        ASSERT_STREQ(mg_value_string(mg_map_at(propertiesM, "name"))->data, "test2");
        ASSERT_EQ(mg_value_bool(mg_map_at(propertiesM, "is_deleted")), 0);

        // Assert properties of Relationship r
        const mg_map *propertiesR = mg_relationship_properties(relationshipR);
        ASSERT_STREQ(mg_value_string(mg_map_at(propertiesR, "attr"))->data, "attr1");

        rows++;
    }
    ASSERT_EQ(rows, 1);
    ASSERT_EQ(status, 0);
}