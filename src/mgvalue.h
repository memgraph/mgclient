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

#ifndef MGCLIENT_MGVALUE_H
#define MGCLIENT_MGVALUE_H

#include "mgallocator.h"
#include "mgclient.h"

typedef struct mg_value mg_value;

typedef struct mg_string {
  uint32_t size;
  char *data;
} mg_string;

typedef struct mg_list {
  uint32_t size;
  uint32_t capacity;
  mg_value **elements;
} mg_list;

typedef struct mg_map {
  uint32_t size;
  uint32_t capacity;
  mg_string **keys;
  mg_value **values;
} mg_map;

typedef struct mg_node {
  int64_t id;
  uint32_t label_count;
  mg_string **labels;
  mg_map *properties;
} mg_node;

typedef struct mg_relationship {
  int64_t id;
  int64_t start_id;
  int64_t end_id;
  mg_string *type;
  mg_map *properties;
} mg_relationship;

typedef struct mg_unbound_relationship {
  int64_t id;
  mg_string *type;
  mg_map *properties;
} mg_unbound_relationship;

typedef struct mg_path {
  uint32_t node_count;
  uint32_t relationship_count;
  uint32_t sequence_length;
  mg_node **nodes;
  mg_unbound_relationship **relationships;
  int64_t *sequence;
} mg_path;

struct mg_value {
  enum mg_value_type type;
  union {
    int bool_v;
    int64_t integer_v;
    double float_v;
    mg_string *string_v;
    mg_list *list_v;
    mg_map *map_v;
    mg_node *node_v;
    mg_relationship *relationship_v;
    mg_unbound_relationship *unbound_relationship_v;
    mg_path *path_v;
  };
};

mg_string *mg_string_alloc(uint32_t size, mg_allocator *allocator);

mg_list *mg_list_alloc(uint32_t size, mg_allocator *allocator);

mg_map *mg_map_alloc(uint32_t size, mg_allocator *allocator);

mg_node *mg_node_alloc(uint32_t label_count, mg_allocator *allocator);

mg_path *mg_path_alloc(uint32_t node_count, uint32_t relationship_count,
                       uint32_t sequence_length, mg_allocator *allocator);

mg_node *mg_node_make(int64_t id, uint32_t label_count, mg_string **labels,
                      mg_map *properties);

mg_relationship *mg_relationship_make(int64_t id, int64_t start_id,
                                      int64_t end_id, mg_string *type,
                                      mg_map *properties);

mg_unbound_relationship *mg_unbound_relationship_make(int64_t id,
                                                      mg_string *type,
                                                      mg_map *properties);

mg_path *mg_path_make(uint32_t node_count, mg_node **nodes,
                      uint32_t relationship_count,
                      mg_unbound_relationship **relationships,
                      uint32_t sequence_length, const int64_t *const sequence);

mg_value *mg_value_copy_ca(const mg_value *val, mg_allocator *allocator);

mg_string *mg_string_copy_ca(const mg_string *string, mg_allocator *allocator);

mg_list *mg_list_copy_ca(const mg_list *list, mg_allocator *allocator);

mg_map *mg_map_copy_ca(const mg_map *map, mg_allocator *allocator);

mg_node *mg_node_copy_ca(const mg_node *node, mg_allocator *allocator);

mg_relationship *mg_relationship_copy_ca(const mg_relationship *rel,
                                         mg_allocator *allocator);

mg_unbound_relationship *mg_unbound_relationship_copy_ca(
    const mg_unbound_relationship *rel, mg_allocator *allocator);

mg_path *mg_path_copy_ca(const mg_path *path, mg_allocator *allocator);

void mg_path_destroy_ca(mg_path *path, mg_allocator *allocator);

void mg_value_destroy_ca(mg_value *val, mg_allocator *allocator);

void mg_string_destroy_ca(mg_string *string, mg_allocator *allocator);

void mg_list_destroy_ca(mg_list *list, mg_allocator *allocator);

void mg_map_destroy_ca(mg_map *map, mg_allocator *allocator);

void mg_node_destroy_ca(mg_node *node, mg_allocator *allocator);

void mg_relationship_destroy_ca(mg_relationship *rel, mg_allocator *allocator);

void mg_unbound_relationship_destroy_ca(mg_unbound_relationship *rel,
                                        mg_allocator *allocator);

void mg_path_destroy_ca(mg_path *path, mg_allocator *allocator);

int mg_string_equal(const mg_string *lhs, const mg_string *rhs);

int mg_map_equal(const mg_map *lhs, const mg_map *rhs);

int mg_node_equal(const mg_node *lhs, const mg_node *rhs);

int mg_relationship_equal(const mg_relationship *lhs,
                          const mg_relationship *rhs);

int mg_unbound_relationship_equal(const mg_unbound_relationship *lhs,
                                  const mg_unbound_relationship *rhs);

int mg_path_equal(const mg_path *lhs, const mg_path *rhs);

int mg_value_equal(const mg_value *lhs, const mg_value *rhs);

extern mg_map mg_empty_map;

#endif
