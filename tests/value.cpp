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

#include <string>

#include <gtest/gtest.h>

extern "C" {
#include "mgallocator.h"
#include "mgclient.h"
#include "mgcommon.h"
#include "mgvalue.h"
}

using namespace std::string_literals;

struct Null {};

bool Equal(const mg_value *lhs, Null) {
  return mg_value_get_type(lhs) == MG_VALUE_TYPE_NULL;
}

bool Equal(const mg_value *lhs, bool rhs) {
  return mg_value_get_type(lhs) == MG_VALUE_TYPE_BOOL &&
         (mg_value_bool(lhs) != 0) == rhs;
}

bool Equal(const mg_value *lhs, int64_t rhs) {
  return mg_value_get_type(lhs) == MG_VALUE_TYPE_INTEGER &&
         mg_value_integer(lhs) == rhs;
}

bool Equal(const mg_value *lhs, double rhs) {
  return mg_value_get_type(lhs) == MG_VALUE_TYPE_FLOAT &&
         mg_value_float(lhs) == rhs;
}

bool Equal(const mg_string *lhs, const std::string &rhs) {
  return mg_string_size(lhs) == rhs.size() &&
         memcmp(mg_string_data(lhs), rhs.data(), rhs.size()) == 0;
}

bool Equal(const mg_value *lhs, const std::string &rhs) {
  return mg_value_get_type(lhs) == MG_VALUE_TYPE_STRING &&
         Equal(mg_value_string(lhs), rhs);
}

TEST(Value, Null) {
  mg_value *val = mg_value_make_null();
  EXPECT_TRUE(Equal(val, Null{}));
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_TRUE(Equal(val2, Null{}));
  mg_value_destroy(val2);
}

TEST(Value, Bool) {
  {
    mg_value *val = mg_value_make_bool(0);
    EXPECT_TRUE(Equal(val, false));
    mg_value *val2 = mg_value_copy(val);
    mg_value_destroy(val);
    EXPECT_TRUE(Equal(val2, false));
    mg_value_destroy(val2);
  }
  {
    mg_value *val = mg_value_make_bool(1);
    EXPECT_TRUE(Equal(val, true));
    mg_value *val2 = mg_value_copy(val);
    mg_value_destroy(val);
    EXPECT_TRUE(Equal(val2, true));
    mg_value_destroy(val2);
  }
}

TEST(Value, Integer) {
  mg_value *val = mg_value_make_integer(3289103);
  EXPECT_TRUE(Equal(val, (int64_t)3289103));
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_TRUE(Equal(val2, (int64_t)3289103));
  mg_value_destroy(val2);
}

TEST(Value, Float) {
  mg_value *val = mg_value_make_float(3.289103);
  EXPECT_TRUE(Equal(val, 3.289103));
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_TRUE(Equal(val2, 3.289103));
  mg_value_destroy(val2);
}

TEST(Value, String) {
  mg_string *str = mg_string_make2(5, "abcde");
  EXPECT_TRUE(Equal(str, "abcde"s));

  mg_string *str2 = mg_string_make("abcdefgh");
  EXPECT_TRUE(Equal(str2, "abcdefgh"s));
  mg_string_destroy(str2);

  mg_string *str3 = mg_string_copy(str);
  mg_string_destroy(str);
  EXPECT_TRUE(Equal(str3, "abcde"s));

  mg_value *val = mg_value_make_string2(str3);
  EXPECT_TRUE(Equal(val, "abcde"s));
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_TRUE(Equal(val2, "abcde"s));
  mg_value_destroy(val2);
}

TEST(Value, List) {
  auto check_list = [](const mg_list *list) {
    ASSERT_EQ(mg_list_size(list), 3u);
    EXPECT_TRUE(Equal(mg_list_at(list, 0), Null{}));
    EXPECT_TRUE(Equal(mg_list_at(list, 1), true));
    EXPECT_TRUE(Equal(mg_list_at(list, 2), "abcde"s));
    EXPECT_EQ(mg_list_at(list, 3), nullptr);
    EXPECT_EQ(mg_list_at(list, 328321), nullptr);
  };

  mg_list *list = mg_list_make_empty(3);

  EXPECT_EQ(mg_list_size(list), 0u);

  EXPECT_EQ(mg_list_append(list, mg_value_make_null()), 0);
  EXPECT_EQ(mg_list_size(list), 1u);

  EXPECT_EQ(mg_list_append(list, mg_value_make_bool(1)), 0);
  EXPECT_EQ(mg_list_size(list), 2u);

  EXPECT_EQ(mg_list_append(list, mg_value_make_string("abcde")), 0);
  EXPECT_EQ(mg_list_size(list), 3u);

  {
    mg_value *value = mg_value_make_float(3.14);
    EXPECT_NE(mg_list_append(list, value), 0);
    EXPECT_EQ(mg_list_size(list), 3u);
    mg_value_destroy(value);
  }

  check_list(list);
  mg_list *list2 = mg_list_copy(list);
  mg_list_destroy(list);
  check_list(list2);

  mg_value *val = mg_value_make_list(list2);
  EXPECT_EQ(mg_value_get_type(val), MG_VALUE_TYPE_LIST);
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_EQ(mg_value_get_type(val2), MG_VALUE_TYPE_LIST);
  check_list(mg_value_list(val2));
  mg_value_destroy(val2);
}

TEST(Value, Map) {
  auto check_map = [](const mg_map *map) {
    ASSERT_EQ(mg_map_size(map), 4u);

    EXPECT_TRUE(Equal(mg_map_at(map, "x"), (int64_t)3));
    EXPECT_TRUE(Equal(mg_map_at(map, "y"), false));
    EXPECT_TRUE(Equal(mg_map_at(map, "key"), "value"s));
    EXPECT_TRUE(Equal(mg_map_at(map, "key2"), "value2"s));

    EXPECT_TRUE(Equal(mg_map_at2(map, 1, "x"), (int64_t)3));
    EXPECT_TRUE(Equal(mg_map_at2(map, 1, "y"), false));
    EXPECT_TRUE(Equal(mg_map_at2(map, 3, "key"), "value"s));
    EXPECT_TRUE(Equal(mg_map_at2(map, 4, "key2"), "value2"s));

    EXPECT_TRUE(Equal(mg_map_key_at(map, 0), "x"s));
    EXPECT_TRUE(Equal(mg_map_key_at(map, 1), "y"s));
    EXPECT_TRUE(Equal(mg_map_key_at(map, 2), "key"s));
    EXPECT_TRUE(Equal(mg_map_key_at(map, 3), "key2"s));

    EXPECT_TRUE(Equal(mg_map_value_at(map, 0), (int64_t)3));
    EXPECT_TRUE(Equal(mg_map_value_at(map, 1), false));
    EXPECT_TRUE(Equal(mg_map_value_at(map, 2), "value"s));
    EXPECT_TRUE(Equal(mg_map_value_at(map, 3), "value2"s));

    EXPECT_EQ(mg_map_at(map, "fjdkslfjdslk"), nullptr);
    EXPECT_EQ(mg_map_key_at(map, 5), nullptr);
    EXPECT_EQ(mg_map_key_at(map, 321321), nullptr);
    EXPECT_EQ(mg_map_value_at(map, 5), nullptr);
    EXPECT_EQ(mg_map_value_at(map, 78789789), nullptr);
  };

  mg_map *map = mg_map_make_empty(4);
  EXPECT_EQ(mg_map_size(map), 0u);

  // Test `insert` and `insert2` with failures for duplicate key.
  {
    EXPECT_EQ(mg_map_insert(map, "x", mg_value_make_integer(3)), 0);
    EXPECT_EQ(mg_map_size(map), 1u);
  }
  {
    mg_value *value = mg_value_make_integer(5);
    EXPECT_NE(mg_map_insert(map, "x", value), 0);
    EXPECT_EQ(mg_map_size(map), 1u);
    mg_value_destroy(value);
  }
  {
    EXPECT_EQ(mg_map_insert2(map, mg_string_make("y"), mg_value_make_bool(0)),
              0);
    EXPECT_EQ(mg_map_size(map), 2u);
  }
  {
    mg_string *key = mg_string_make("y");
    mg_value *value = mg_value_make_float(3.14);
    EXPECT_NE(mg_map_insert2(map, key, value), 0);
    EXPECT_EQ(mg_map_size(map), 2u);
    mg_string_destroy(key);
    mg_value_destroy(value);
  }

  // Test `insert_unsafe` and `insert_unsafe2`.
  {
    EXPECT_EQ(mg_map_insert_unsafe(map, "key", mg_value_make_string("value")),
              0);
    EXPECT_EQ(mg_map_size(map), 3u);
  }
  {
    EXPECT_EQ(mg_map_insert_unsafe2(map, mg_string_make("key2"),
                                    mg_value_make_string("value2")),
              0);
    EXPECT_EQ(mg_map_size(map), 4u);
  }

  // All insertions should fail now because the map is full.
  {
    mg_value *value = mg_value_make_null();
    EXPECT_NE(mg_map_insert(map, "k1", value), 0);
    EXPECT_EQ(mg_map_size(map), 4u);
    mg_value_destroy(value);
  }
  {
    mg_string *key = mg_string_make("k2");
    mg_value *value = mg_value_make_null();
    EXPECT_NE(mg_map_insert2(map, key, value), 0);
    EXPECT_EQ(mg_map_size(map), 4u);
    mg_string_destroy(key);
    mg_value_destroy(value);
  }
  {
    mg_value *value = mg_value_make_null();
    EXPECT_NE(mg_map_insert_unsafe(map, "k3", value), 0);
    EXPECT_EQ(mg_map_size(map), 4u);
    mg_value_destroy(value);
  }
  {
    mg_string *key = mg_string_make("k4");
    mg_value *value = mg_value_make_null();
    EXPECT_NE(mg_map_insert_unsafe2(map, key, value), 0);
    EXPECT_EQ(mg_map_size(map), 4u);
    mg_string_destroy(key);
    mg_value_destroy(value);
  }

  check_map(map);
  mg_map *map2 = mg_map_copy(map);
  mg_map_destroy(map);
  check_map(map2);

  mg_value *val = mg_value_make_map(map2);
  EXPECT_EQ(mg_value_get_type(val), MG_VALUE_TYPE_MAP);
  check_map(mg_value_map(val));
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_EQ(mg_value_get_type(val2), MG_VALUE_TYPE_MAP);
  check_map(mg_value_map(val2));
  mg_value_destroy(val2);
}

TEST(Value, Node) {
  auto check_node = [](const mg_node *node) {
    EXPECT_EQ(mg_node_id(node), 1234);
    EXPECT_EQ(mg_node_label_count(node), 2u);
    EXPECT_TRUE(Equal(mg_node_label_at(node, 0), "Label1"s));
    EXPECT_TRUE(Equal(mg_node_label_at(node, 1), "Label2"s));
    EXPECT_EQ(mg_node_label_at(node, 2), nullptr);
    EXPECT_EQ(mg_node_label_at(node, 328192), nullptr);

    const mg_map *props = mg_node_properties(node);
    EXPECT_EQ(mg_map_size(props), 2u);
    EXPECT_TRUE(Equal(mg_map_key_at(props, 0), "x"s));
    EXPECT_TRUE(Equal(mg_map_key_at(props, 1), "y"s));
    EXPECT_TRUE(Equal(mg_map_value_at(props, 0), (int64_t)1));
    EXPECT_TRUE(Equal(mg_map_value_at(props, 1), (int64_t)2));
  };

  mg_string *labels[2] = {mg_string_make("Label1"), mg_string_make("Label2")};
  mg_map *props = mg_map_make_empty(2);
  mg_map_insert_unsafe(props, "x", mg_value_make_integer(1));
  mg_map_insert_unsafe(props, "y", mg_value_make_integer(2));

  mg_node *node = mg_node_make(1234, 2, labels, props);
  check_node(node);
  mg_node *node2 = mg_node_copy(node);
  mg_node_destroy(node);
  check_node(node2);

  mg_value *val = mg_value_make_node(node2);
  EXPECT_EQ(mg_value_get_type(val), MG_VALUE_TYPE_NODE);
  check_node(mg_value_node(val));
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_EQ(mg_value_get_type(val2), MG_VALUE_TYPE_NODE);
  check_node(mg_value_node(val2));
  mg_value_destroy(val2);
}

TEST(Value, Relationship) {
  auto check_relationship = [](const mg_relationship *rel) {
    EXPECT_EQ(mg_relationship_id(rel), 567);
    EXPECT_EQ(mg_relationship_start_id(rel), 10);
    EXPECT_EQ(mg_relationship_end_id(rel), 20);
    EXPECT_TRUE(Equal(mg_relationship_type(rel), "EDGE"s));

    const mg_map *props = mg_relationship_properties(rel);
    EXPECT_EQ(mg_map_size(props), 2u);
    EXPECT_TRUE(Equal(mg_map_key_at(props, 0), "x"s));
    EXPECT_TRUE(Equal(mg_map_key_at(props, 1), "y"s));
    EXPECT_TRUE(Equal(mg_map_value_at(props, 0), (int64_t)1));
    EXPECT_TRUE(Equal(mg_map_value_at(props, 1), (int64_t)2));
  };

  mg_map *props = mg_map_make_empty(2);
  mg_map_insert_unsafe(props, "x", mg_value_make_integer(1));
  mg_map_insert_unsafe(props, "y", mg_value_make_integer(2));

  mg_relationship *rel =
      mg_relationship_make(567, 10, 20, mg_string_make("EDGE"), props);
  check_relationship(rel);
  mg_relationship *rel2 = mg_relationship_copy(rel);
  mg_relationship_destroy(rel);
  check_relationship(rel2);

  mg_value *val = mg_value_make_relationship(rel2);
  EXPECT_EQ(mg_value_get_type(val), MG_VALUE_TYPE_RELATIONSHIP);
  check_relationship(mg_value_relationship(val));
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_EQ(mg_value_get_type(val2), MG_VALUE_TYPE_RELATIONSHIP);
  check_relationship(mg_value_relationship(val2));
  mg_value_destroy(val2);
}

TEST(Value, UnboundRelationship) {
  auto check_unbound_relationship = [](const mg_unbound_relationship *rel) {
    EXPECT_EQ(mg_unbound_relationship_id(rel), 567);
    EXPECT_TRUE(Equal(mg_unbound_relationship_type(rel), "EDGE"s));

    const mg_map *props = mg_unbound_relationship_properties(rel);
    EXPECT_EQ(mg_map_size(props), 2u);
    EXPECT_TRUE(Equal(mg_map_key_at(props, 0), "x"s));
    EXPECT_TRUE(Equal(mg_map_key_at(props, 1), "y"s));
    EXPECT_TRUE(Equal(mg_map_value_at(props, 0), (int64_t)1));
    EXPECT_TRUE(Equal(mg_map_value_at(props, 1), (int64_t)2));
  };

  mg_map *props = mg_map_make_empty(2);
  mg_map_insert_unsafe(props, "x", mg_value_make_integer(1));
  mg_map_insert_unsafe(props, "y", mg_value_make_integer(2));

  mg_unbound_relationship *rel =
      mg_unbound_relationship_make(567, mg_string_make("EDGE"), props);
  check_unbound_relationship(rel);
  mg_unbound_relationship *rel2 = mg_unbound_relationship_copy(rel);
  mg_unbound_relationship_destroy(rel);
  check_unbound_relationship(rel2);

  mg_value *val = mg_value_make_unbound_relationship(rel2);
  EXPECT_EQ(mg_value_get_type(val), MG_VALUE_TYPE_UNBOUND_RELATIONSHIP);
  check_unbound_relationship(mg_value_unbound_relationship(val));
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_EQ(mg_value_get_type(val2), MG_VALUE_TYPE_UNBOUND_RELATIONSHIP);
  check_unbound_relationship(mg_value_unbound_relationship(val2));
  mg_value_destroy(val2);
}

TEST(Value, Path) {
  const int64_t indices[] = {1, 1, -2, 2, 3, 0, 1, 1, -4, 3, 5, 3};

  auto check_path = [](const mg_path *path) {
    EXPECT_EQ(mg_path_length(path), 6u);
    EXPECT_EQ(mg_node_id(mg_path_node_at(path, 0)), 1);
    EXPECT_EQ(mg_node_id(mg_path_node_at(path, 1)), 2);
    EXPECT_EQ(mg_node_id(mg_path_node_at(path, 2)), 3);
    EXPECT_EQ(mg_node_id(mg_path_node_at(path, 3)), 1);
    EXPECT_EQ(mg_node_id(mg_path_node_at(path, 4)), 2);
    EXPECT_EQ(mg_node_id(mg_path_node_at(path, 5)), 4);
    EXPECT_EQ(mg_node_id(mg_path_node_at(path, 6)), 4);
    EXPECT_EQ(mg_path_node_at(path, 7), nullptr);
    EXPECT_EQ(mg_path_node_at(path, 328190321), nullptr);

    EXPECT_EQ(mg_unbound_relationship_id(mg_path_relationship_at(path, 0)), 12);
    EXPECT_EQ(mg_unbound_relationship_id(mg_path_relationship_at(path, 1)), 32);
    EXPECT_EQ(mg_unbound_relationship_id(mg_path_relationship_at(path, 2)), 31);
    EXPECT_EQ(mg_unbound_relationship_id(mg_path_relationship_at(path, 3)), 12);
    EXPECT_EQ(mg_unbound_relationship_id(mg_path_relationship_at(path, 4)), 42);
    EXPECT_EQ(mg_unbound_relationship_id(mg_path_relationship_at(path, 5)), 44);
    EXPECT_EQ(mg_path_relationship_at(path, 6), nullptr);
    EXPECT_EQ(mg_path_relationship_at(path, 38290187), nullptr);

    EXPECT_EQ(mg_path_relationship_reversed_at(path, 0), 0);
    EXPECT_EQ(mg_path_relationship_reversed_at(path, 1), 1);
    EXPECT_EQ(mg_path_relationship_reversed_at(path, 2), 0);
    EXPECT_EQ(mg_path_relationship_reversed_at(path, 3), 0);
    EXPECT_EQ(mg_path_relationship_reversed_at(path, 4), 1);
    EXPECT_EQ(mg_path_relationship_reversed_at(path, 5), 0);
    EXPECT_EQ(mg_path_relationship_reversed_at(path, 6), -1);
    EXPECT_EQ(mg_path_relationship_reversed_at(path, 83291038), -1);
  };
  mg_node *nodes[] = {mg_node_make(1, 0, nullptr, mg_map_make_empty(0)),
                      mg_node_make(2, 0, nullptr, mg_map_make_empty(0)),
                      mg_node_make(3, 0, nullptr, mg_map_make_empty(0)),
                      mg_node_make(4, 0, nullptr, mg_map_make_empty(0))};
  mg_unbound_relationship *relationships[] = {
      mg_unbound_relationship_make(12, mg_string_make("EDGE"),
                                   mg_map_make_empty(0)),
      mg_unbound_relationship_make(32, mg_string_make("EDGE"),
                                   mg_map_make_empty(0)),
      mg_unbound_relationship_make(31, mg_string_make("EDGE"),
                                   mg_map_make_empty(0)),
      mg_unbound_relationship_make(42, mg_string_make("EDGE"),
                                   mg_map_make_empty(0)),
      mg_unbound_relationship_make(44, mg_string_make("EDGE"),
                                   mg_map_make_empty(0))};

  mg_path *path = mg_path_make(4, nodes, 5, relationships,
                               sizeof(indices) / sizeof(int64_t), indices);
  check_path(path);
  mg_path *path2 = mg_path_copy(path);
  mg_path_destroy(path);
  check_path(path2);

  mg_value *val = mg_value_make_path(path2);
  EXPECT_EQ(mg_value_get_type(val), MG_VALUE_TYPE_PATH);
  check_path(mg_value_path(val));
  mg_value *val2 = mg_value_copy(val);
  mg_value_destroy(val);
  EXPECT_EQ(mg_value_get_type(val2), MG_VALUE_TYPE_PATH);
  check_path(mg_value_path(val2));
  mg_value_destroy(val2);
}

TEST(Value, Date) {
  {
    mg_date *date = mg_date_alloc(&mg_system_allocator);
    date->days = 1;
    EXPECT_EQ(mg_date_days(date), static_cast<int64_t>(1));
    mg_date *date2 = mg_date_copy(date);
    mg_date_destroy(date);
    EXPECT_EQ(mg_date_days(date2), static_cast<int64_t>(1));
    mg_date_destroy(date2);
  }
}

TEST(Value, Time) {
  {
    mg_time *time = mg_time_alloc(&mg_system_allocator);
    time->nanoseconds = 1;
    time->tz_offset_seconds = 1;
    EXPECT_EQ(mg_time_nanoseconds(time), static_cast<int64_t>(1));
    EXPECT_EQ(mg_time_tz_offset_seconds(time), static_cast<int64_t>(1));
    mg_time *time2 = mg_time_copy(time);
    mg_time_destroy(time);
    EXPECT_EQ(mg_time_nanoseconds(time2), static_cast<int64_t>(1));
    EXPECT_EQ(mg_time_tz_offset_seconds(time2), static_cast<int64_t>(1));
    mg_time_destroy(time2);
  }
}

TEST(Value, LocalTime) {
  {
    mg_local_time *local_time = mg_local_time_alloc(&mg_system_allocator);
    local_time->nanoseconds = 1;
    EXPECT_EQ(mg_local_time_nanoseconds(local_time), static_cast<int64_t>(1));
    mg_local_time *local_time2 = mg_local_time_copy(local_time);
    mg_local_time_destroy(local_time);
    EXPECT_EQ(mg_local_time_nanoseconds(local_time2), static_cast<int64_t>(1));
    mg_local_time_destroy(local_time2);
  }
}

TEST(Value, DateTime) {
  {
    mg_date_time *date_time = mg_date_time_alloc(&mg_system_allocator);
    date_time->seconds = 1;
    date_time->nanoseconds = 1;
    date_time->tz_offset_minutes = 1;
    EXPECT_EQ(mg_date_time_seconds(date_time), static_cast<int64_t>(1));
    EXPECT_EQ(mg_date_time_nanoseconds(date_time), static_cast<int64_t>(1));
    EXPECT_EQ(mg_date_time_tz_offset_minutes(date_time),
              static_cast<int64_t>(1));
    mg_date_time *date_time2 = mg_date_time_copy(date_time);
    mg_date_time_destroy(date_time);
    EXPECT_EQ(mg_date_time_seconds(date_time2), static_cast<int64_t>(1));
    EXPECT_EQ(mg_date_time_nanoseconds(date_time2), static_cast<int64_t>(1));
    EXPECT_EQ(mg_date_time_tz_offset_minutes(date_time2),
              static_cast<int64_t>(1));
    mg_date_time_destroy(date_time2);
  }
}

TEST(Value, DateTimeZoneId) {
  {
    mg_date_time_zone_id *date_time_zone_id =
        mg_date_time_zone_id_alloc(&mg_system_allocator);
    date_time_zone_id->seconds = 1;
    date_time_zone_id->nanoseconds = 1;
    date_time_zone_id->tz_id = 1;
    EXPECT_EQ(mg_date_time_zone_id_seconds(date_time_zone_id),
              static_cast<int64_t>(1));
    EXPECT_EQ(mg_date_time_zone_id_nanoseconds(date_time_zone_id),
              static_cast<int64_t>(1));
    EXPECT_EQ(mg_date_time_zone_id_tz_id(date_time_zone_id),
              static_cast<int64_t>(1));
    mg_date_time_zone_id *date_time_zone_id2 =
        mg_date_time_zone_id_copy(date_time_zone_id);
    mg_date_time_zone_id_destroy(date_time_zone_id);
    EXPECT_EQ(mg_date_time_zone_id_seconds(date_time_zone_id2),
              static_cast<int64_t>(1));
    EXPECT_EQ(mg_date_time_zone_id_nanoseconds(date_time_zone_id2),
              static_cast<int64_t>(1));
    EXPECT_EQ(mg_date_time_zone_id_tz_id(date_time_zone_id2),
              static_cast<int64_t>(1));
    mg_date_time_zone_id_destroy(date_time_zone_id2);
  }
}

TEST(Value, LocalDateTime) {
  {
    mg_local_date_time *local_date_time =
        mg_local_date_time_alloc(&mg_system_allocator);
    local_date_time->seconds = 1;
    local_date_time->nanoseconds = 1;
    EXPECT_EQ(mg_local_date_time_seconds(local_date_time),
              static_cast<int64_t>(1));
    EXPECT_EQ(mg_local_date_time_nanoseconds(local_date_time),
              static_cast<int64_t>(1));
    mg_local_date_time *local_date_time2 =
        mg_local_date_time_copy(local_date_time);
    mg_local_date_time_destroy(local_date_time);
    EXPECT_EQ(mg_local_date_time_seconds(local_date_time2),
              static_cast<int64_t>(1));
    EXPECT_EQ(mg_local_date_time_nanoseconds(local_date_time2),
              static_cast<int64_t>(1));
    mg_local_date_time_destroy(local_date_time2);
  }
}

TEST(Value, Duration) {
  {
    mg_duration *duration = mg_duration_alloc(&mg_system_allocator);
    duration->months = 1;
    duration->days = 1;
    duration->seconds = 1;
    duration->nanoseconds = 1;
    EXPECT_EQ(mg_duration_months(duration), static_cast<int64_t>(1));
    EXPECT_EQ(mg_duration_days(duration), static_cast<int64_t>(1));
    EXPECT_EQ(mg_duration_seconds(duration), static_cast<int64_t>(1));
    EXPECT_EQ(mg_duration_nanoseconds(duration), static_cast<int64_t>(1));
    mg_duration *duration2 = mg_duration_copy(duration);
    mg_duration_destroy(duration);
    EXPECT_EQ(mg_duration_months(duration2), static_cast<int64_t>(1));
    EXPECT_EQ(mg_duration_days(duration2), static_cast<int64_t>(1));
    EXPECT_EQ(mg_duration_seconds(duration2), static_cast<int64_t>(1));
    EXPECT_EQ(mg_duration_nanoseconds(duration2), static_cast<int64_t>(1));
    mg_duration_destroy(duration2);
  }
}

TEST(Value, Point2d) {
  {
    mg_point_2d *point_2d = mg_point_2d_alloc(&mg_system_allocator);
    point_2d->srid = 1;
    point_2d->x = 1.0;
    point_2d->y = 1.0;
    EXPECT_EQ(mg_point_2d_srid(point_2d), static_cast<int64_t>(1));
    EXPECT_EQ(mg_point_2d_x(point_2d), 1.0);
    EXPECT_EQ(mg_point_2d_y(point_2d), 1.0);
    mg_point_2d *point_2d2 = mg_point_2d_copy(point_2d);
    mg_point_2d_destroy(point_2d);
    EXPECT_EQ(mg_point_2d_srid(point_2d2), static_cast<int64_t>(1));
    EXPECT_EQ(mg_point_2d_x(point_2d2), 1.0);
    EXPECT_EQ(mg_point_2d_y(point_2d2), 1.0);
    mg_point_2d_destroy(point_2d2);
  }
}

TEST(Value, Point3d) {
  {
    mg_point_3d *point_3d = mg_point_3d_alloc(&mg_system_allocator);
    point_3d->srid = 1;
    point_3d->x = 1.0;
    point_3d->y = 1.0;
    point_3d->z = 1.0;
    EXPECT_EQ(mg_point_3d_srid(point_3d), static_cast<int64_t>(1));
    EXPECT_EQ(mg_point_3d_x(point_3d), 1.0);
    EXPECT_EQ(mg_point_3d_y(point_3d), 1.0);
    EXPECT_EQ(mg_point_3d_z(point_3d), 1.0);
    mg_point_3d *point_3d2 = mg_point_3d_copy(point_3d);
    mg_point_3d_destroy(point_3d);
    EXPECT_EQ(mg_point_3d_srid(point_3d2), static_cast<int64_t>(1));
    EXPECT_EQ(mg_point_3d_x(point_3d2), 1.0);
    EXPECT_EQ(mg_point_3d_y(point_3d2), 1.0);
    EXPECT_EQ(mg_point_3d_z(point_3d2), 1.0);
    mg_point_3d_destroy(point_3d2);
  }
}
