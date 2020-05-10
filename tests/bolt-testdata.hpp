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

#pragma once

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mgclient.h"

using namespace std::string_literals;

class ValueTestParam {
 public:
  mg_value *decoded;
  std::string encoded;

  ValueTestParam(mg_value *decoded, std::string encoded)
      : decoded(decoded), encoded(encoded) {}

  ValueTestParam(const ValueTestParam &rhs)
      : decoded(mg_value_copy(rhs.decoded)), encoded(rhs.encoded) {}

  ~ValueTestParam() {
    if (decoded) {
      mg_value_destroy(decoded);
    }
  }
};

std::vector<ValueTestParam> NullTestCases() {
  return std::vector<ValueTestParam>{{mg_value_make_null(), "\xC0"s}};
}

std::vector<ValueTestParam> BoolTestCases() {
  return std::vector<ValueTestParam>{{mg_value_make_bool(0), "\xC2"s},
                                     {mg_value_make_bool(1), "\xC3"s},
                                     {mg_value_make_bool(12345), "\xC3"s},
                                     {mg_value_make_bool(-12345), "\xC3"s}};
}

std::vector<ValueTestParam> IntegerTestCases() {
  return std::vector<ValueTestParam>{
      {mg_value_make_integer(0), "\x00"s},
      {mg_value_make_integer(1), "\x01"s},
      {mg_value_make_integer(-1), "\xFF"s},
      {mg_value_make_integer(10), "\x0A"s},
      {mg_value_make_integer(-10), "\xF6"s},
      {mg_value_make_integer(-33), "\xC8\xDF"s},
      {mg_value_make_integer(31352), "\xC9\x7A\x78"s},
      {mg_value_make_integer(-3285), "\xC9\xF3\x2B"s},
      {mg_value_make_integer(731528356), "\xCA\x2B\x9A\x3C\xA4"s},
      {mg_value_make_integer(-456395151), "\xCA\xE4\xCB\xF6\x71"s},
      {mg_value_make_integer(INT64_C(5684726540577289134)),
       "\xCB\x4E\xE4\x34\xAB\x70\x58\x33\xAE"s},
      {mg_value_make_integer(INT64_C(-4001895993540242495)),
       "\xCB\xC8\x76\x68\xCB\xFC\xF9\x93\xC1"s},
      {mg_value_make_integer(-16), "\xF0"s},       // MG_TINY_INT_MIN
      {mg_value_make_integer(INT8_MAX), "\x7F"s},  // MG_TINY_INT_MAX
      {mg_value_make_integer(-17), "\xC8\xEF"s},   // MG_TINY_INT_MIN - 1
      {mg_value_make_integer(INT8_MIN), "\xC8\x80"s},
      {mg_value_make_integer(INT8_MIN - 1), "\xC9\xFF\x7F"s},
      {mg_value_make_integer(INT8_MAX + 1), "\xC9\x00\x80"s},
      {mg_value_make_integer(INT16_MIN), "\xC9\x80\x00"s},
      {mg_value_make_integer(INT16_MAX), "\xC9\x7F\xFF"s},
      {mg_value_make_integer(INT16_MIN - 1), "\xCA\xFF\xFF\x7F\xFF"s},
      {mg_value_make_integer(INT16_MAX + 1), "\xCA\x00\x00\x80\x00"s},
      {mg_value_make_integer(INT32_MIN), "\xCA\x80\x00\x00\x00"s},
      {mg_value_make_integer(INT32_MAX), "\xCA\x7F\xFF\xFF\xFF"s},
      {mg_value_make_integer((int64_t)INT32_MIN - 1),
       "\xCB\xFF\xFF\xFF\xFF\x7F\xFF\xFF\xFF"s},
      {mg_value_make_integer((int64_t)INT32_MAX + 1),
       "\xCB\x00\x00\x00\x00\x80\x00\x00\x00"s},
      {mg_value_make_integer(INT64_MIN),
       "\xCB\x80\x00\x00\x00\x00\x00\x00\x00"s},
      {mg_value_make_integer(INT64_MAX),
       "\xCB\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF"s}};
}

std::vector<ValueTestParam> FloatTestCases() {
  return std::vector<ValueTestParam>{
      {mg_value_make_float(1.0), "\xC1\x3F\xF0\x00\x00\x00\x00\x00\x00"s},
      {mg_value_make_float(-1.0), "\xC1\xBF\xF0\x00\x00\x00\x00\x00\x00"s},
      {mg_value_make_float(1.56e-11), "\xC1\x3D\xB1\x27\x02\x77\x8C\xC4\x37"s},
      {mg_value_make_float(-3.1415), "\xC1\xC0\x09\x21\xCA\xC0\x83\x12\x6F"s}};
}

/// Encoding of a container is just encoding of its size concatenated to
/// encodings of its elements. There are 4 size classes: TINY (<= 15), SIZE_8 (<
/// 2^8), SIZE_16 (< 2^16), SIZE_32 (< 2^32). In array SIZE we have a range of
/// sizes to test if container sizes are encoded correctly according to size
/// class. There is a utility function `GetEncodedSize` to provide encoded
/// container size for each test case, which is then used in
/// `ContainerTestCases` functions.
size_t SIZE[] = {0, 1, 10, 15, 16, 130, 255, 256, 10000, 65535, 65536, 130000};

enum class SizeClass { TINY, SIZE_8, SIZE_16, SIZE_32 };

SizeClass SIZE_CLASS[] = {
    SizeClass::TINY,    SizeClass::TINY,    SizeClass::TINY,
    SizeClass::TINY,    SizeClass::SIZE_8,  SizeClass::SIZE_8,
    SizeClass::SIZE_8,  SizeClass::SIZE_16, SizeClass::SIZE_16,
    SizeClass::SIZE_16, SizeClass::SIZE_32, SizeClass::SIZE_32};

std::string ENCODED_SIZE[] = {"not applicable",
                              "not applicable",
                              "not applicable",
                              "not applicable",
                              "\x10"s,
                              "\x82"s,
                              "\xFF"s,
                              "\x01\x00"s,
                              "\x27\x10"s,
                              "\xFF\xFF"s,
                              "\x00\x01\x00\x00"s,
                              "\x00\x01\xFB\xD0"s};

int NUM_INPUTS = sizeof(SIZE) / sizeof(size_t);

enum class ContainerType { STRING, LIST, MAP };

std::string GetEncodedSize(int idx, ContainerType type) {
  char marker_tiny;
  char marker_8;
  char marker_16;
  char marker_32;
  auto &&markers = std::tie(marker_tiny, marker_8, marker_16, marker_32);
  switch (type) {
    case ContainerType::STRING:
      markers = std::make_tuple('\x80', '\xD0', '\xD1', '\xD2');
      break;
    case ContainerType::LIST:
      markers = std::make_tuple('\x90', '\xD4', '\xD5', '\xD6');
      break;
    case ContainerType::MAP:
      markers = std::make_tuple('\xA0', '\xD8', '\xD9', '\xDa');
      break;
  }
  switch (SIZE_CLASS[idx]) {
    case SizeClass::TINY:
      return std::string(1, char(marker_tiny + SIZE[idx]));
    case SizeClass::SIZE_8:
      return std::string(1, marker_8) + ENCODED_SIZE[idx];
    case SizeClass::SIZE_16:
      return std::string(1, marker_16) + ENCODED_SIZE[idx];
    case SizeClass::SIZE_32:
      return std::string(1, marker_32) + ENCODED_SIZE[idx];
  }
}

/// `GetElement` and `GetElementEncoding` for a little variety in container
/// elements.
mg_value *GetElement(int idx) {
  idx %= 6;
  switch (idx) {
    case 0:
      return mg_value_make_null();
    case 1:
      return mg_value_make_integer(123456789);
    case 2:
      return mg_value_make_float(1.28);
    case 3:
      return mg_value_make_string("string");
    case 4: {
      mg_list *list = mg_list_make_empty(3);
      mg_list_append(list, mg_value_make_integer(1));
      mg_list_append(list, mg_value_make_integer(2));
      mg_list_append(list, mg_value_make_integer(3));
      return mg_value_make_list(list);
    }
    case 5: {
      mg_map *map = mg_map_make_empty(2);
      mg_map_insert_unsafe(map, "x", mg_value_make_integer(1));
      mg_map_insert_unsafe(map, "y", mg_value_make_integer(2));
      return mg_value_make_map(map);
    }
  }
}

std::string GetElementEncoding(int idx) {
  idx %= 6;
  switch (idx) {
    case 0:
      return "\xC0"s;
    case 1:
      return "\xCA\x07\x5B\xCD\x15"s;
    case 2:
      return "\xC1\x3F\xF4\x7A\xE1\x47\xAE\x14\x7B"s;
    case 3:
      return "\x86string"s;
    case 4:
      return "\x93\x01\x02\x03";
    case 5:
      return "\xA2\x81x\x01\x81y\x02";
  }
}

std::vector<ValueTestParam> StringTestCases() {
  std::vector<ValueTestParam> test_cases;
  for (int i = 0; i < NUM_INPUTS; ++i) {
    std::string data;
    /// String 'abcdefhijklmnopqrstuvwxyzabcdefhijklmnopq...'
    for (int j = 0; j < SIZE[i]; ++j) data.push_back((char)(j % 26 + 'a'));
    std::string encoded_size = GetEncodedSize(i, ContainerType::STRING);
    test_cases.push_back(
        {mg_value_make_string(data.c_str()), encoded_size + data});
  }
  return test_cases;
}

std::vector<ValueTestParam> ListTestCases() {
  std::vector<ValueTestParam> inputs;
  for (int i = 0; i < NUM_INPUTS; ++i) {
    std::string encoded = GetEncodedSize(i, ContainerType::LIST);
    mg_list *list = mg_list_make_empty(SIZE[i]);
    for (int j = 0; j < SIZE[i]; ++j) {
      encoded += GetElementEncoding(j);
      mg_list_append(list, GetElement(j));
    }
    inputs.push_back({mg_value_make_list(list), encoded});
  }
  return inputs;
}

std::vector<ValueTestParam> MapTestCases() {
  std::vector<ValueTestParam> inputs;
  for (int i = 0; i < NUM_INPUTS; ++i) {
    std::string encoded = GetEncodedSize(i, ContainerType::MAP);
    mg_map *map = mg_map_make_empty(SIZE[i]);
    for (int j = 0; j < SIZE[i]; ++j) {
      std::string key = "k" + std::to_string(j);
      encoded.push_back('\x80' + key.size());
      encoded += key;
      encoded += GetElementEncoding(j);
      mg_map_insert_unsafe(map, key.c_str(), GetElement(j));
    }

    inputs.push_back({mg_value_make_map(map), encoded});
  }
  return inputs;
}

std::vector<ValueTestParam> NodeTestCases() {
  std::vector<ValueTestParam> inputs;

  {
    mg_string *labels[] = {};
    mg_map *props = mg_map_make_empty(0);
    mg_node *node = mg_node_make(12345, 0, labels, props);
    inputs.push_back(
        {mg_value_make_node(node), "\xB3\x4E\xC9\x30\x39\x90\xA0"});
  }

  {
    mg_string *labels[] = {mg_string_make("Label1"), mg_string_make("Label2")};
    mg_map *props = mg_map_make_empty(2);
    mg_map_insert(props, "x", mg_value_make_integer(1));
    mg_map_insert(props, "y", mg_value_make_string("ipsilon"));
    mg_node *node = mg_node_make(12345, 2, labels, props);
    inputs.push_back({mg_value_make_node(node),
                      "\xB3\x4E\xC9\x30\x39\x92\x86Label1\x86Label2\xA2\x81x"
                      "\x01\x81y\x87ipsilon"s});
  }
  return inputs;
}

std::vector<ValueTestParam> RelationshipTestCases() {
  std::vector<ValueTestParam> inputs;
  {
    mg_string *type = mg_string_make("Edge");

    mg_map *props = mg_map_make_empty(2);
    mg_map_insert(props, "x", mg_value_make_integer(1));
    mg_map_insert(props, "y", mg_value_make_integer(2));

    mg_relationship *rel =
        mg_relationship_make(1234, 5678, 372819, type, props);
    inputs.push_back({mg_value_make_relationship(rel),
                      "\xB5\x52\xC9\x04\xD2\xC9\x16\x2E\xCA\x00\x05\xB0\x53\x84"
                      "Edge\xA2\x81x\x01\x81y\x02"s});
  }
  return inputs;
}

std::vector<ValueTestParam> UnboundRelationshipTestCases() {
  std::vector<ValueTestParam> inputs;
  {
    mg_string *type = mg_string_make("Edge");

    mg_map *props = mg_map_make_empty(2);
    mg_map_insert(props, "x", mg_value_make_integer(1));
    mg_map_insert(props, "y", mg_value_make_integer(2));

    mg_unbound_relationship *rel =
        mg_unbound_relationship_make(1234, type, props);
    inputs.push_back({mg_value_make_unbound_relationship(rel),
                      "\xB3\x72\xC9\x04\xD2\x84"
                      "Edge\xA2\x81x\x01\x81y\x02"s});
  }
  return inputs;
}

std::vector<ValueTestParam> PathTestCases() {
  std::vector<ValueTestParam> inputs;
  {
    mg_node *nodes[] = {mg_node_make(1, 0, NULL, mg_map_make_empty(0)),
                        mg_node_make(2, 0, NULL, mg_map_make_empty(0)),
                        mg_node_make(3, 0, NULL, mg_map_make_empty(0)),
                        mg_node_make(4, 0, NULL, mg_map_make_empty(0))};
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
    const int64_t indices[] = {1, 1, -2, 2, 3, 0, 1, 1, -4, 3, 5, 3};

    mg_path *path = mg_path_make(4, nodes, 5, relationships,
                                 sizeof(indices) / sizeof(int64_t), indices);

    auto encoded_node = [](char id) {
      return "\xB3\x4E"s + std::string(1, id) + "\x90\xA0"s;
    };

    auto encoded_edge = [](char id) {
      return "\xB3\x72"s + std::string(1, id) + "\x84"s + "EDGE\xA0"s;
    };

    std::string encoded_path;
    encoded_path += "\xB3\x50"s;
    encoded_path += "\x94"s;
    encoded_path += encoded_node(1);
    encoded_path += encoded_node(2);
    encoded_path += encoded_node(3);
    encoded_path += encoded_node(4);
    encoded_path += "\x95"s;
    encoded_path += encoded_edge(12);
    encoded_path += encoded_edge(32);
    encoded_path += encoded_edge(31);
    encoded_path += encoded_edge(42);
    encoded_path += encoded_edge(44);
    encoded_path += "\x9C";
    encoded_path += "\x01\x01\xFE\x02\x03\x00\x01\x01\xFC\x03\x05\x03"s;

    inputs.push_back({mg_value_make_path(path), encoded_path});
  }
  return inputs;
}
