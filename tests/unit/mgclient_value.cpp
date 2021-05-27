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

#include <stdlib.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "mgclient-value.hpp"
#include "mgclient.h"

using namespace std;

namespace mg {

TEST(ValueTest, BasicTypes) {
  Value value_null;
  Value value_bool1(true);
  Value value_bool2(false);
  Value value_int1(-13);
  Value value_int2(static_cast<int64_t>((1LL << 60)));
  Value value_double(3.14159);
  Value value_string1("test");
  Value value_string2(std::string("test"));

  // assert correctness:
  ASSERT_EQ(value_null.type(), Value::Type::Null);
  ASSERT_EQ(value_bool1.type(), Value::Type::Bool);
  ASSERT_EQ(value_bool2.type(), Value::Type::Bool);
  ASSERT_EQ(value_int1.type(), Value::Type::Int);
  ASSERT_EQ(value_int2.type(), Value::Type::Int);
  ASSERT_EQ(value_double.type(), Value::Type::Double);
  ASSERT_EQ(value_string1.type(), Value::Type::String);
  ASSERT_EQ(value_string2.type(), Value::Type::String);

  ASSERT_EQ(value_bool1.ValueBool(), true);
  ASSERT_EQ(value_bool2.ValueBool(), false);
  ASSERT_EQ(value_int1.ValueInt(), -13);
  ASSERT_EQ(value_int2.ValueInt(), 1LL << 60);
  ASSERT_EQ(value_double.ValueDouble(), 3.14159);
  ASSERT_EQ(value_string1.ValueString(), "test");
  ASSERT_EQ(value_string2.ValueString(), "test");

  // compare:
  ASSERT_EQ(value_string1, value_string2);
  ASSERT_NE(value_string1, value_bool1);
  ASSERT_NE(value_bool1, value_bool2);
  ASSERT_EQ(value_string1.AsConstValue(), value_string2.AsConstValue());
  ASSERT_EQ(value_string1.AsConstValue(), value_string2);
  ASSERT_EQ(value_string1, value_string2.AsConstValue());
  ASSERT_NE(value_int1.AsConstValue(), value_int2.AsConstValue());
  ASSERT_NE(value_int1.AsConstValue(), value_int2);
  ASSERT_NE(value_int1, value_int2.AsConstValue());
}

TEST(ValueTest, CopyValue) {
  Value value1(100);
  Value value2(value1);

  ASSERT_NE(value1.ptr(), nullptr);
  ASSERT_NE(value2.ptr(), nullptr);
  ASSERT_NE(value1.ptr(), value2.ptr());
  ASSERT_EQ(value1, value2);
}

TEST(ValueTest, MoveValue) {
  Value value1(100);
  auto ptr = value1.ptr();
  Value value2(std::move(value1));

  ASSERT_NE(ptr, nullptr);
  ASSERT_EQ(value1.ptr(), nullptr);
  ASSERT_EQ(value2.ptr(), ptr);
}

TEST(ValueTest, ListConstruction) {
  List inner_list(2);
  inner_list.Append(Value(1));
  inner_list.Append(Value(false));

  List list(4);
  list.Append(Value("hey"));
  list.Append(Value(3.14));
  list.Append(Value(std::move(inner_list)));

  ASSERT_EQ(list.size(), 3u);
  ASSERT_EQ(list[0], Value("hey"));
  ASSERT_EQ(list[1], Value(3.14));
  ASSERT_EQ(list[2].type(), Value::Type::List);
  ASSERT_EQ(list[2].ValueList().size(), 2u);

  ConstList const_list = list.AsConstList();
  ASSERT_EQ(const_list.size(), 3u);
  ASSERT_EQ(const_list[0], Value("hey"));
  ASSERT_EQ(const_list[1], Value(3.14));
  ASSERT_EQ(const_list[2].type(), Value::Type::List);
  ASSERT_EQ(const_list[2].ValueList().size(), 2u);
}

TEST(ValueTest, ListIterate) {
  List list(4);
  list.Append(Value("hey"));
  list.Append(Value(3.14));
  list.Append(Value(true));

  std::vector<ConstValue> values;
  for (const auto value : list) {
    values.push_back(value);
  }

  ASSERT_EQ(values.size(), 3u);
  ASSERT_EQ(values[0], Value("hey"));
  ASSERT_EQ(values[1], Value(3.14));
  ASSERT_EQ(values[2], Value(true));

  values.clear();
  for (const auto value : list.AsConstList()) {
    values.push_back(value);
  }

  ASSERT_EQ(values.size(), 3u);
  ASSERT_EQ(values[0], Value("hey"));
  ASSERT_EQ(values[1], Value(3.14));
  ASSERT_EQ(values[2], Value(true));
}

TEST(ValueTest, ListAppendCopiedValue) {
  Value value(3);
  List list(1);
  list.Append(value.AsConstValue());
  ASSERT_NE(list[0].ptr(), nullptr);
  ASSERT_NE(list[0].ptr(), value.ptr());
  ASSERT_EQ(list[0], value);
}

TEST(ValueTest, ListAppendMovedValue) {
  Value value(3);
  auto ptr = value.ptr();
  List list(1);
  list.Append(std::move(value));
  ASSERT_EQ(list[0].ptr(), ptr);
  ASSERT_EQ(value.ptr(), nullptr);
}

TEST(ValueTest, ListComparison) {
  List list1(4);
  List list2(3);
  List list3(3);

  list1.Append(Value(1));
  list1.Append(Value(3.14));
  list1.Append(Value(false));

  // the same for list2:
  list2.Append(Value(1));
  list2.Append(Value(3.14));
  list2.Append(Value(false));

  // list3 is a bit different:
  list3.Append(Value(3.14));
  list3.Append(Value(true));
  list3.Append(Value("ciao"));

  ConstList const_list1 = list1.AsConstList();
  ConstList const_list2 = list2.AsConstList();
  ConstList const_list3 = list3.AsConstList();

  ASSERT_EQ(list1, list2);
  ASSERT_EQ(list1, const_list2);
  ASSERT_EQ(const_list1, list2);
  ASSERT_EQ(const_list1, const_list2);

  ASSERT_NE(list1, list3);
  ASSERT_NE(list1, const_list3);
  ASSERT_NE(const_list1, list3);
  ASSERT_NE(const_list1, const_list3);
}

TEST(ValueTest, ValueFromList) {
  List list(2);
  list.Append(Value(1));
  list.Append(Value(2));
  auto ptr = list.ptr();
  Value value(std::move(list));

  ASSERT_EQ(value.type(), Value::Type::List);
  ConstList value_list = value.ValueList();
  ASSERT_EQ(value_list.ptr(), ptr);
  ASSERT_EQ(list.ptr(), nullptr);
  ASSERT_EQ(value_list[0], Value(1));
  ASSERT_EQ(value_list[1], Value(2));
}

TEST(ValueTest, MapConstruction) {
  Map map(4);
  map.Insert("key 1", Value(1));
  map.Insert("key 2", Value(3.14));
  map.Insert("key 3", Value(false));

  ASSERT_EQ(map.size(), 3u);
  ASSERT_EQ(map["key 1"], Value(1));
  ASSERT_EQ(map["key 2"], Value(3.14));
  ASSERT_EQ(map["key 3"], Value(false));

  ConstMap const_map = map.AsConstMap();
  ASSERT_EQ(const_map.size(), 3u);
  ASSERT_EQ(const_map["key 1"], Value(1));
  ASSERT_EQ(const_map["key 2"], Value(3.14));
  ASSERT_EQ(const_map["key 3"], Value(false));
}

TEST(ValueTest, MapIterate) {
  Map map(4);
  map.Insert("key 1", Value(1));
  map.Insert("key 2", Value("two"));
  map.Insert("key 3", Value(3.0));

  std::vector<std::pair<std::string, ConstValue>> values;
  for (const auto [key, value] : map) {
    values.emplace_back(key, value);
  }

  ASSERT_EQ(values.size(), 3u);
  ASSERT_EQ(values[0].first, "key 1");
  ASSERT_EQ(values[0].second, Value(1));
  ASSERT_EQ(values[1].first, "key 2");
  ASSERT_EQ(values[1].second, Value("two"));
  ASSERT_EQ(values[2].first, "key 3");
  ASSERT_EQ(values[2].second, Value(3.0));

  values.clear();
  for (const auto [key, value] : map.AsConstMap()) {
    values.emplace_back(key, value);
  }

  ASSERT_EQ(values.size(), 3u);
  ASSERT_EQ(values[0].first, "key 1");
  ASSERT_EQ(values[0].second, Value(1));
  ASSERT_EQ(values[1].first, "key 2");
  ASSERT_EQ(values[1].second, Value("two"));
  ASSERT_EQ(values[2].first, "key 3");
  ASSERT_EQ(values[2].second, Value(3.0));
}

TEST(ValueTest, MapInsertCopiedValue) {
  Value value(100);
  Map map(1);
  map.Insert("key", value.AsConstValue());

  ASSERT_NE(value.ptr(), nullptr);
  ASSERT_NE(map["key"].ptr(), value.ptr());
  ASSERT_EQ(map["key"], value);
}

TEST(ValueTest, MapInsertMovedValue) {
  Value value(100);
  auto ptr = value.ptr();
  Map map(1);
  map.Insert("key", std::move(value));
  ASSERT_EQ(value.ptr(), nullptr);
  ASSERT_EQ(map["key"].ptr(), ptr);
}

TEST(ValueTest, MapComparison) {
  Map map1(4);
  Map map2(3);
  Map map3(3);

  map1.Insert("key 1", Value("ciao"));
  map1.Insert("key 2", Value(13));
  map1.Insert("key 3", Value(false));

  // map2 is the same, but insertion order is different:
  map2.Insert("key 2", Value(13));
  map2.Insert("key 3", Value(false));
  map2.Insert("key 1", Value("ciao"));

  // map3 is a bit different:
  map3.Insert("key 1", Value("ciao"));
  map3.Insert("key 2", Value(false));
  map3.Insert("key 3", Value(13));

  ConstMap const_map1 = map1.AsConstMap();
  ConstMap const_map2 = map2.AsConstMap();
  ConstMap const_map3 = map3.AsConstMap();

  ASSERT_EQ(map1, map2);
  ASSERT_EQ(map1, const_map2);
  ASSERT_EQ(const_map1, map2);
  ASSERT_EQ(const_map1, const_map2);

  ASSERT_NE(map1, map3);
  ASSERT_NE(map1, const_map3);
  ASSERT_NE(const_map1, map3);
  ASSERT_NE(const_map1, const_map3);
}

TEST(ValueTest, MapFind) {
  Map map(1);
  map.Insert("key 1", Value(1));

  auto it = map.find("key 1");

  ASSERT_FALSE(it == map.end());
  ASSERT_EQ((*it).first, "key 1");
  ASSERT_EQ((*it).second, Value(1));

  ASSERT_TRUE(map.find("key 2") == map.end());
}

TEST(ValueTest, ValueFromMap) {
  Map map(1);
  map.Insert("key 1", Value(13));
  auto ptr = map.ptr();
  Value value(std::move(map));

  ASSERT_EQ(value.type(), Value::Type::Map);
  ConstMap value_map = value.ValueMap();
  ASSERT_EQ(value_map.ptr(), ptr);
  ASSERT_EQ(map.ptr(), nullptr);
  auto it = value_map.find("key 1");
  ASSERT_TRUE(it != value_map.end());
  ASSERT_EQ((*it).second, Value(13));
}

}  // namespace mg
