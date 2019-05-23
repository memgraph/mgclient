// Copyright (c) 2016-2019 Memgraph Ltd. [https://memgraph.com]
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

#include "mgvalue.h"

#include <stdlib.h>
#include <string.h>

#include "mgclient.h"
#include "mgconstants.h"

mg_string *mg_string_alloc(uint32_t size, mg_allocator *allocator) {
  char *block = mg_allocator_malloc(allocator, sizeof(mg_string) + size);
  if (!block) {
    return NULL;
  }
  mg_string *str = (mg_string *)block;
  str->data = block + sizeof(mg_string);
  return str;
}

mg_list *mg_list_alloc(uint32_t size, mg_allocator *allocator) {
  size_t elements_size = size * sizeof(mg_value *);
  char *block = mg_allocator_malloc(allocator, sizeof(mg_list) + elements_size);
  if (!block) {
    return NULL;
  }
  mg_list *list = (mg_list *)block;
  list->elements = (mg_value **)(block + sizeof(mg_list));
  return list;
}

mg_map *mg_map_alloc(uint32_t size, mg_allocator *allocator) {
  size_t keys_size = size * sizeof(mg_string *);
  size_t values_size = size * sizeof(mg_value *);
  char *block =
      mg_allocator_malloc(allocator, sizeof(mg_map) + keys_size + values_size);
  if (!block) {
    return NULL;
  }
  mg_map *map = (mg_map *)block;
  map->keys = (mg_string **)(block + sizeof(mg_map));
  map->values = (mg_value **)(block + sizeof(mg_map) + keys_size);
  return map;
}

mg_node *mg_node_alloc(uint32_t label_count, mg_allocator *allocator) {
  size_t labels_size = label_count * sizeof(mg_string *);
  char *block = mg_allocator_malloc(allocator, sizeof(mg_node) + labels_size);
  if (!block) {
    return NULL;
  }
  mg_node *node = (mg_node *)block;
  node->labels = (mg_string **)(block + sizeof(mg_node));
  return node;
}

mg_path *mg_path_alloc(uint32_t node_count, uint32_t relationship_count,
                       uint32_t sequence_length, mg_allocator *allocator) {
  size_t nodes_size = node_count * sizeof(mg_node *);
  size_t relationships_size =
      relationship_count * sizeof(mg_unbound_relationship *);
  size_t sequence_size = sequence_length * sizeof(int64_t);
  char *block =
      mg_allocator_malloc(allocator, sizeof(mg_path) + nodes_size +
                                         relationships_size + sequence_size);
  if (!block) {
    return NULL;
  }
  mg_path *path = (mg_path *)block;
  path->nodes = (mg_node **)(block + sizeof(mg_path));
  path->relationships =
      (mg_unbound_relationship **)(block + sizeof(mg_path) + nodes_size);
  path->sequence =
      (int64_t *)(block + sizeof(mg_path) + nodes_size + relationships_size);
  return path;
}

mg_value *mg_value_make_null() {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_NULL;
  return value;
}

mg_value *mg_value_make_bool(int val) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_BOOL;
  value->bool_v = val;
  return value;
}

mg_value *mg_value_make_integer(int64_t val) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_INTEGER;
  value->integer_v = val;
  return value;
}

mg_value *mg_value_make_float(double val) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_FLOAT;
  value->float_v = val;
  return value;
}

mg_value *mg_value_make_string(const char *str) {
  mg_string *tstr = mg_string_make(str);
  if (!tstr) {
    return NULL;
  }
  return mg_value_make_string2(tstr);
}

mg_value *mg_value_make_string2(mg_string *str) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_STRING;
  value->string_v = str;
  return value;
}

mg_value *mg_value_make_list(mg_list *list) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_LIST;
  value->list_v = list;
  return value;
}

mg_value *mg_value_make_map(mg_map *map) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_MAP;
  value->map_v = map;
  return value;
}

mg_value *mg_value_make_node(mg_node *node) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_NODE;
  value->node_v = node;
  return value;
}

mg_value *mg_value_make_relationship(mg_relationship *rel) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_RELATIONSHIP;
  value->relationship_v = rel;
  return value;
}

mg_value *mg_value_make_unbound_relationship(mg_unbound_relationship *rel) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_UNBOUND_RELATIONSHIP;
  value->unbound_relationship_v = rel;
  return value;
}

mg_value *mg_value_make_path(mg_path *path) {
  mg_value *value = mg_allocator_malloc(&mg_system_allocator, sizeof(mg_value));
  if (!value) {
    return NULL;
  }
  value->type = MG_VALUE_TYPE_PATH;
  value->path_v = path;
  return value;
}

enum mg_value_type mg_value_get_type(const mg_value *val) { return val->type; }

int mg_value_bool(const mg_value *val) { return val->bool_v; }

int64_t mg_value_integer(const mg_value *val) { return val->integer_v; }

double mg_value_float(const mg_value *val) { return val->float_v; }

const mg_string *mg_value_string(const mg_value *val) { return val->string_v; }

const mg_list *mg_value_list(const mg_value *val) { return val->list_v; }

const mg_map *mg_value_map(const mg_value *val) { return val->map_v; }

const mg_node *mg_value_node(const mg_value *val) { return val->node_v; }

const mg_relationship *mg_value_relationship(const mg_value *val) {
  return val->relationship_v;
}

const mg_unbound_relationship *mg_value_unbound_relationship(
    const mg_value *val) {
  return val->unbound_relationship_v;
}

const mg_path *mg_value_path(const mg_value *val) { return val->path_v; }

mg_value *mg_value_copy_ca(const mg_value *val, mg_allocator *allocator) {
  if (!val) {
    return NULL;
  }
  mg_value *new_val = mg_allocator_malloc(allocator, sizeof(mg_value));
  if (!new_val) {
    return NULL;
  }
  new_val->type = val->type;
  switch (val->type) {
    case MG_VALUE_TYPE_NULL:
      break;
    case MG_VALUE_TYPE_BOOL:
      new_val->bool_v = val->bool_v;
      break;
    case MG_VALUE_TYPE_INTEGER:
      new_val->integer_v = val->integer_v;
      break;
    case MG_VALUE_TYPE_FLOAT:
      new_val->float_v = val->float_v;
      break;
    case MG_VALUE_TYPE_STRING:
      new_val->string_v = mg_string_copy_ca(val->string_v, allocator);
      if (!new_val->string_v) {
        goto cleanup;
      }
      break;
    case MG_VALUE_TYPE_LIST:
      new_val->list_v = mg_list_copy_ca(val->list_v, allocator);
      if (!new_val->list_v) {
        goto cleanup;
      }
      break;
    case MG_VALUE_TYPE_MAP:
      new_val->map_v = mg_map_copy_ca(val->map_v, allocator);
      if (!new_val->map_v) {
        goto cleanup;
      }
      break;
    case MG_VALUE_TYPE_NODE:
      new_val->node_v = mg_node_copy_ca(val->node_v, allocator);
      if (!new_val->node_v) {
        goto cleanup;
      }
      break;
    case MG_VALUE_TYPE_RELATIONSHIP:
      new_val->relationship_v =
          mg_relationship_copy_ca(val->relationship_v, allocator);
      if (!new_val->relationship_v) {
        goto cleanup;
      }
      break;
    case MG_VALUE_TYPE_UNBOUND_RELATIONSHIP:
      new_val->unbound_relationship_v = mg_unbound_relationship_copy_ca(
          val->unbound_relationship_v, allocator);
      if (!new_val->unbound_relationship_v) {
        goto cleanup;
      }
      break;
    case MG_VALUE_TYPE_PATH:
      new_val->path_v = mg_path_copy_ca(val->path_v, allocator);
      if (!new_val->path_v) {
        goto cleanup;
      }
      break;
    case MG_VALUE_TYPE_UNKNOWN:
      break;
  }
  return new_val;

cleanup:
  mg_allocator_free(&mg_system_allocator, new_val);
  return NULL;
}

mg_value *mg_value_copy(const mg_value *val) {
  return mg_value_copy_ca(val, &mg_system_allocator);
}

void mg_value_destroy_ca(mg_value *val, mg_allocator *allocator) {
  if (!val) {
    return;
  }
  switch (val->type) {
    case MG_VALUE_TYPE_NULL:
    case MG_VALUE_TYPE_BOOL:
    case MG_VALUE_TYPE_INTEGER:
    case MG_VALUE_TYPE_FLOAT:
      break;
    case MG_VALUE_TYPE_STRING:
      mg_string_destroy_ca(val->string_v, allocator);
      break;
    case MG_VALUE_TYPE_LIST:
      mg_list_destroy_ca(val->list_v, allocator);
      break;
    case MG_VALUE_TYPE_MAP:
      mg_map_destroy_ca(val->map_v, allocator);
      break;
    case MG_VALUE_TYPE_NODE:
      mg_node_destroy_ca(val->node_v, allocator);
      break;
    case MG_VALUE_TYPE_RELATIONSHIP:
      mg_relationship_destroy_ca(val->relationship_v, allocator);
      break;
    case MG_VALUE_TYPE_UNBOUND_RELATIONSHIP:
      mg_unbound_relationship_destroy_ca(val->unbound_relationship_v,
                                         allocator);
      break;
    case MG_VALUE_TYPE_PATH:
      mg_path_destroy_ca(val->path_v, allocator);
      break;
    case MG_VALUE_TYPE_UNKNOWN:
      break;
  }
  mg_allocator_free(allocator, val);
}

void mg_value_destroy(mg_value *val) {
  if (!val) {
    return;
  }
  mg_value_destroy_ca(val, &mg_system_allocator);
}

mg_string *mg_string_make(const char *str) {
  size_t size = strlen(str);
  if (size >= UINT32_MAX) return NULL;
  return mg_string_make2((uint32_t)size, str);
}

mg_string *mg_string_make2(uint32_t len, const char *data) {
  mg_string *str = mg_string_alloc(len, &mg_system_allocator);
  if (!str) {
    return NULL;
  }
  str->size = len;
  memcpy(str->data, data, len);
  return str;
}

const char *mg_string_data(const mg_string *str) { return str->data; }

uint32_t mg_string_size(const mg_string *str) { return str->size; }

mg_string *mg_string_copy_ca(const mg_string *src, mg_allocator *allocator) {
  if (!src) {
    return NULL;
  }
  mg_string *str = mg_string_alloc(src->size, allocator);
  if (!str) {
    return NULL;
  }
  str->size = src->size;
  memcpy(str->data, src->data, src->size);
  return str;
}

mg_string *mg_string_copy(const mg_string *str) {
  return mg_string_copy_ca(str, &mg_system_allocator);
}

void mg_string_destroy_ca(mg_string *str, mg_allocator *allocator) {
  if (!str) {
    return;
  }
  mg_allocator_free(allocator, str);
}

void mg_string_destroy(mg_string *str) {
  mg_string_destroy_ca(str, &mg_system_allocator);
}

static int mg_string_eq(uint32_t size1, const char *str1, uint32_t size2,
                        const char *str2) {
  if (size1 != size2) return 0;
  return memcmp(str1, str2, size1) == 0;
}

mg_list *mg_list_make_empty(uint32_t capacity) {
  mg_list *list = mg_list_alloc(capacity, &mg_system_allocator);
  if (!list) {
    return NULL;
  }
  list->size = 0;
  list->capacity = capacity;
  return list;
}

int mg_list_append(mg_list *list, mg_value *value) {
  if (list->size >= list->capacity) {
    return MG_ERROR_CONTAINER_FULL;
  }
  list->elements[list->size] = value;
  list->size++;
  return 0;
}

uint32_t mg_list_size(const mg_list *list) { return list->size; }

const mg_value *mg_list_at(const mg_list *list, uint32_t pos) {
  if (pos < list->size) {
    return list->elements[pos];
  } else {
    return NULL;
  }
}

mg_list *mg_list_copy_ca(const mg_list *list, mg_allocator *allocator) {
  if (!list) {
    return NULL;
  }
  mg_list *nlist = mg_list_alloc(list->size, allocator);
  if (!nlist) {
    return NULL;
  }
  nlist->capacity = list->size;
  nlist->size = 0;
  for (uint32_t i = 0; i < list->size; ++i) {
    nlist->elements[i] = mg_value_copy_ca(list->elements[i], allocator);
    if (!nlist->elements[i]) {
      goto cleanup;
    }
    nlist->size++;
  }
  return nlist;

cleanup:
  for (uint32_t i = 0; i < nlist->size; ++i) {
    mg_value_destroy(nlist->elements[i]);
  }
  mg_allocator_free(allocator, nlist);
  return NULL;
}

mg_list *mg_list_copy(const mg_list *list) {
  return mg_list_copy_ca(list, &mg_system_allocator);
}

void mg_list_destroy_ca(mg_list *list, mg_allocator *allocator) {
  if (!list) {
    return;
  }
  for (uint32_t i = 0; i < list->size; ++i) {
    mg_value_destroy_ca(list->elements[i], allocator);
  }
  mg_allocator_free(allocator, list);
}

void mg_list_destroy(mg_list *list) {
  mg_list_destroy_ca(list, &mg_system_allocator);
}

mg_map *mg_map_make_empty(uint32_t capacity) {
  mg_map *map = mg_map_alloc(capacity, &mg_system_allocator);
  if (!map) {
    return NULL;
  }
  map->size = 0;
  map->capacity = capacity;
  return map;
}

static uint32_t mg_map_find_key(const mg_map *map, uint32_t key_size,
                                const char *key_data) {
  for (uint32_t i = 0; i < map->size; ++i) {
    if (mg_string_eq(map->keys[i]->size, map->keys[i]->data, key_size,
                     key_data)) {
      return i;
    }
  }
  return map->size;
}

static void mg_map_append(mg_map *map, mg_string *key, mg_value *value) {
  map->keys[map->size] = key;
  map->values[map->size] = value;
  map->size++;
}

int mg_map_insert(mg_map *map, const char *key_str, mg_value *value) {
  size_t key_size = strlen(key_str);
  if (key_size >= UINT32_MAX) {
    return MG_ERROR_SIZE_EXCEEDED;
  }
  if (map->size >= map->capacity) {
    return MG_ERROR_CONTAINER_FULL;
  }
  if (mg_map_find_key(map, (uint32_t)key_size, key_str) != map->size) {
    return MG_ERROR_DUPLICATE_KEY;
  }
  mg_string *key = mg_string_make2((uint32_t)key_size, key_str);
  if (!key) {
    return MG_ERROR_OOM;
  }
  mg_map_append(map, key, value);
  return 0;
}

int mg_map_insert2(mg_map *map, mg_string *key, mg_value *value) {
  if (map->size >= map->capacity) {
    return MG_ERROR_CONTAINER_FULL;
  }
  if (mg_map_find_key(map, key->size, key->data) != map->size) {
    return MG_ERROR_DUPLICATE_KEY;
  }
  mg_map_append(map, key, value);
  return 0;
}

int mg_map_insert_unsafe(mg_map *map, const char *key_str, mg_value *value) {
  if (map->size >= map->capacity) {
    return MG_ERROR_CONTAINER_FULL;
  }
  size_t key_len = strlen(key_str);
  if (key_len >= UINT32_MAX) {
    return MG_ERROR_SIZE_EXCEEDED;
  }
  mg_string *key = mg_string_make2((uint32_t)key_len, key_str);
  if (key == NULL) {
    return MG_ERROR_OOM;
  }
  mg_map_append(map, key, value);
  return 0;
}

int mg_map_insert_unsafe2(mg_map *map, mg_string *key, mg_value *value) {
  if (map->size >= map->capacity) {
    return MG_ERROR_CONTAINER_FULL;
  }
  mg_map_append(map, key, value);
  return 0;
}

const mg_value *mg_map_at(const mg_map *map, const char *key_str) {
  size_t key_size = strlen(key_str);
  if (key_size >= UINT32_MAX) {
    return NULL;
  }
  return mg_map_at2(map, (uint32_t)key_size, key_str);
}

const mg_value *mg_map_at2(const mg_map *map, uint32_t key_size,
                           const char *key_data) {
  uint32_t pos = mg_map_find_key(map, key_size, key_data);
  if (pos != map->size) {
    return map->values[pos];
  }
  return NULL;
}

uint32_t mg_map_size(const mg_map *map) { return map->size; }

const mg_string *mg_map_key_at(const mg_map *map, uint32_t pos) {
  if (pos < map->size) {
    return map->keys[pos];
  } else {
    return NULL;
  }
}

const mg_value *mg_map_value_at(const mg_map *map, uint32_t pos) {
  if (pos < map->size) {
    return map->values[pos];
  } else {
    return NULL;
  }
}

mg_map *mg_map_copy_ca(const mg_map *map, mg_allocator *allocator) {
  if (!map) {
    return NULL;
  }
  mg_map *nmap = mg_map_alloc(map->size, allocator);
  if (!nmap) {
    return NULL;
  }
  nmap->capacity = map->size;
  nmap->size = map->size;
  uint32_t keys_copied = 0;
  uint32_t values_copied = 0;
  for (uint32_t i = 0; i < map->size; ++i) {
    nmap->keys[i] = mg_string_copy_ca(map->keys[i], allocator);
    if (!nmap->keys[i]) {
      goto cleanup;
    }
    keys_copied++;
    nmap->values[i] = mg_value_copy_ca(map->values[i], allocator);
    if (!nmap->values[i]) {
      goto cleanup;
    }
    values_copied++;
  }
  return nmap;

cleanup:
  for (uint32_t i = 0; i < keys_copied; ++i) {
    mg_string_destroy(map->keys[i]);
  }
  for (uint32_t i = 0; i < values_copied; ++i) {
    mg_value_destroy(map->values[i]);
  }
  mg_allocator_free(&mg_system_allocator, nmap);
  return NULL;
}

mg_map *mg_map_copy(const mg_map *map) {
  return mg_map_copy_ca(map, &mg_system_allocator);
}

void mg_map_destroy_ca(mg_map *map, mg_allocator *allocator) {
  if (!map || map == &mg_empty_map) {
    return;
  }
  for (uint32_t i = 0; i < map->size; ++i) {
    mg_string_destroy_ca(map->keys[i], allocator);
    mg_value_destroy_ca(map->values[i], allocator);
  }
  mg_allocator_free(allocator, map);
}

void mg_map_destroy(mg_map *map) {
  mg_map_destroy_ca(map, &mg_system_allocator);
}

int64_t mg_node_id(const mg_node *node) { return node->id; }

uint32_t mg_node_label_count(const mg_node *node) { return node->label_count; }

const mg_string *mg_node_label_at(const mg_node *node, uint32_t pos) {
  if (pos < node->label_count) {
    return node->labels[pos];
  } else {
    return NULL;
  }
}

const mg_map *mg_node_properties(const mg_node *node) {
  return node->properties;
}

mg_node *mg_node_copy_ca(const mg_node *node, mg_allocator *allocator) {
  if (!node) {
    return NULL;
  }
  mg_node *nnode = mg_node_alloc(node->label_count, &mg_system_allocator);
  if (!nnode) {
    return NULL;
  }
  nnode->id = node->id;
  nnode->label_count = 0;
  for (uint32_t i = 0; i < node->label_count; ++i) {
    nnode->labels[i] = mg_string_copy_ca(node->labels[i], allocator);
    if (!nnode->labels[i]) {
      goto cleanup;
    }
    nnode->label_count++;
  }
  nnode->properties = mg_map_copy_ca(node->properties, allocator);
  if (!nnode->properties) {
    goto cleanup;
  }
  return nnode;

cleanup:
  for (uint32_t i = 0; i < nnode->label_count; ++i) {
    mg_string_destroy(nnode->labels[i]);
  }
  mg_allocator_free(&mg_system_allocator, nnode);
  return NULL;
}

mg_node *mg_node_copy(const mg_node *node) {
  return mg_node_copy_ca(node, &mg_system_allocator);
}

void mg_node_destroy_ca(mg_node *node, mg_allocator *allocator) {
  if (!node) {
    return;
  }
  for (uint32_t i = 0; i < node->label_count; ++i) {
    mg_string_destroy_ca(node->labels[i], allocator);
  }
  mg_map_destroy_ca(node->properties, allocator);
  mg_allocator_free(allocator, node);
}

void mg_node_destroy(mg_node *node) {
  mg_node_destroy_ca(node, &mg_system_allocator);
}

int64_t mg_relationship_id(const mg_relationship *rel) { return rel->id; }

int64_t mg_relationship_start_id(const mg_relationship *rel) {
  return rel->start_id;
}

int64_t mg_relationship_end_id(const mg_relationship *rel) {
  return rel->end_id;
}

const mg_string *mg_relationship_type(const mg_relationship *rel) {
  return rel->type;
}

const mg_map *mg_relationship_properties(const mg_relationship *rel) {
  return rel->properties;
}

mg_relationship *mg_relationship_copy_ca(const mg_relationship *rel,
                                         mg_allocator *allocator) {
  if (!rel) {
    return NULL;
  }
  mg_relationship *nrel =
      mg_allocator_malloc(&mg_system_allocator, sizeof(mg_relationship));
  if (!nrel) {
    return NULL;
  }
  nrel->id = rel->id;
  nrel->start_id = rel->start_id;
  nrel->end_id = rel->end_id;
  nrel->type = mg_string_copy_ca(rel->type, allocator);
  if (!nrel->type) {
    goto cleanup;
  }
  nrel->properties = mg_map_copy_ca(rel->properties, allocator);
  if (!nrel->properties) {
    goto cleanup_type;
  }
  return nrel;

cleanup_type:
  mg_string_destroy(nrel->type);

cleanup:
  mg_allocator_free(&mg_system_allocator, nrel);
  return NULL;
}

mg_relationship *mg_relationship_copy(const mg_relationship *rel) {
  return mg_relationship_copy_ca(rel, &mg_system_allocator);
}

void mg_relationship_destroy_ca(mg_relationship *rel, mg_allocator *allocator) {
  if (!rel) {
    return;
  }
  mg_string_destroy_ca(rel->type, allocator);
  mg_map_destroy_ca(rel->properties, allocator);
  mg_allocator_free(allocator, rel);
}

void mg_relationship_destroy(mg_relationship *rel) {
  mg_relationship_destroy_ca(rel, &mg_system_allocator);
}

int64_t mg_unbound_relationship_id(const mg_unbound_relationship *rel) {
  return rel->id;
}

const mg_string *mg_unbound_relationship_type(
    const mg_unbound_relationship *rel) {
  return rel->type;
}

const mg_map *mg_unbound_relationship_properties(
    const mg_unbound_relationship *rel) {
  return rel->properties;
}

mg_unbound_relationship *mg_unbound_relationship_copy_ca(
    const mg_unbound_relationship *rel, mg_allocator *allocator) {
  mg_unbound_relationship *nrel = mg_allocator_malloc(
      &mg_system_allocator, sizeof(mg_unbound_relationship));
  if (!nrel) {
    return NULL;
  }
  nrel->id = rel->id;
  nrel->type = mg_string_copy_ca(rel->type, allocator);
  if (!nrel->type) {
    goto cleanup;
  }
  nrel->properties = mg_map_copy_ca(rel->properties, allocator);
  if (!nrel->properties) {
    goto cleanup_type;
  }
  return nrel;

cleanup_type:
  mg_string_destroy(nrel->type);

cleanup:
  mg_allocator_free(&mg_system_allocator, nrel);
  return NULL;
}

mg_unbound_relationship *mg_unbound_relationship_copy(
    const mg_unbound_relationship *rel) {
  return mg_unbound_relationship_copy_ca(rel, &mg_system_allocator);
}

void mg_unbound_relationship_destroy_ca(mg_unbound_relationship *rel,
                                        mg_allocator *allocator) {
  if (!rel) {
    return;
  }
  mg_string_destroy_ca(rel->type, allocator);
  mg_map_destroy_ca(rel->properties, allocator);
  mg_allocator_free(allocator, rel);
}

void mg_unbound_relationship_destroy(mg_unbound_relationship *rel) {
  mg_unbound_relationship_destroy_ca(rel, &mg_system_allocator);
}

uint32_t mg_path_length(const mg_path *path) {
  return path->sequence_length / 2;
}

const mg_node *mg_path_node_at(const mg_path *path, uint32_t pos) {
  if (pos <= path->sequence_length / 2) {
    if (pos == 0) {
      return path->nodes[0];
    }
    return path->nodes[path->sequence[2 * pos - 1]];
  } else {
    return NULL;
  }
}

const mg_unbound_relationship *mg_path_relationship_at(const mg_path *path,
                                                       uint32_t pos) {
  if (pos < path->sequence_length / 2) {
    int64_t idx = path->sequence[2 * pos];
    if (idx < 0) {
      idx = -idx;
    }
    return path->relationships[idx - 1];
  } else {
    return NULL;
  }
}

int mg_path_relationship_reversed_at(const mg_path *path, uint32_t pos) {
  if (pos < path->sequence_length / 2) {
    int64_t idx = path->sequence[2 * pos];
    if (idx < 0) {
      return 1;
    } else {
      return 0;
    }
  } else {
    return -1;
  }
}

mg_path *mg_path_copy_ca(const mg_path *path, mg_allocator *allocator) {
  mg_path *npath = mg_path_alloc(path->node_count, path->relationship_count,
                                 path->sequence_length, &mg_system_allocator);
  if (!npath) {
    return NULL;
  }
  npath->node_count = 0;
  for (uint32_t i = 0; i < path->node_count; ++i) {
    npath->nodes[i] = mg_node_copy_ca(path->nodes[i], allocator);
    if (!npath->nodes[i]) {
      goto cleanup;
    }
    npath->node_count++;
  }
  npath->relationship_count = 0;
  for (uint32_t i = 0; i < path->relationship_count; ++i) {
    npath->relationships[i] =
        mg_unbound_relationship_copy_ca(path->relationships[i], allocator);
    if (!npath->relationships[i]) {
      goto cleanup;
    }
    npath->relationship_count++;
  }
  npath->sequence_length = path->sequence_length;
  memcpy(npath->sequence, path->sequence,
         path->sequence_length * sizeof(int64_t));
  return npath;

cleanup:
  for (uint32_t i = 0; i < npath->node_count; ++i) {
    mg_node_destroy(npath->nodes[i]);
  }
  for (uint32_t i = 0; i < npath->relationship_count; ++i) {
    mg_unbound_relationship_destroy(npath->relationships[i]);
  }
  mg_allocator_free(&mg_system_allocator, npath);
  return NULL;
}

mg_path *mg_path_copy(const mg_path *path) {
  return mg_path_copy_ca(path, &mg_system_allocator);
}

void mg_path_destroy_ca(mg_path *path, mg_allocator *allocator) {
  if (!path) {
    return;
  }
  for (uint32_t i = 0; i < path->node_count; ++i) {
    mg_node_destroy_ca(path->nodes[i], allocator);
  }
  for (uint32_t i = 0; i < path->relationship_count; ++i) {
    mg_unbound_relationship_destroy_ca(path->relationships[i], allocator);
  }
  mg_allocator_free(allocator, path);
}

void mg_path_destroy(mg_path *path) {
  mg_path_destroy_ca(path, &mg_system_allocator);
}

mg_node *mg_node_make(int64_t id, uint32_t label_count, mg_string **labels,
                      mg_map *properties) {
  mg_node *node = mg_node_alloc(label_count, &mg_system_allocator);
  if (!node) {
    return NULL;
  }
  node->id = id;
  node->label_count = label_count;
  memcpy(node->labels, labels, label_count * sizeof(mg_string *));
  node->properties = properties;
  return node;
}

mg_relationship *mg_relationship_make(int64_t id, int64_t start_id,
                                      int64_t end_id, mg_string *type,
                                      mg_map *properties) {
  mg_relationship *rel =
      mg_allocator_malloc(&mg_system_allocator, sizeof(mg_relationship));
  if (!rel) {
    return NULL;
  }
  rel->id = id;
  rel->start_id = start_id;
  rel->end_id = end_id;
  rel->type = type;
  rel->properties = properties;
  return rel;
}

mg_unbound_relationship *mg_unbound_relationship_make(int64_t id,
                                                      mg_string *type,
                                                      mg_map *properties) {
  mg_unbound_relationship *rel = mg_allocator_malloc(
      &mg_system_allocator, sizeof(mg_unbound_relationship));
  if (!rel) {
    return NULL;
  }
  rel->id = id;
  rel->type = type;
  rel->properties = properties;
  return rel;
}

mg_path *mg_path_make(uint32_t node_count, mg_node **nodes,
                      uint32_t relationship_count,
                      mg_unbound_relationship **relationships,
                      uint32_t sequence_length, const int64_t *const sequence) {
  mg_path *path = mg_path_alloc(node_count, relationship_count, sequence_length,
                                &mg_system_allocator);
  if (!path) {
    return NULL;
  }
  path->node_count = node_count;
  memcpy(path->nodes, nodes, node_count * sizeof(mg_node *));
  path->relationship_count = relationship_count;
  memcpy(path->relationships, relationships,
         relationship_count * sizeof(mg_unbound_relationship *));
  path->sequence_length = sequence_length;
  memcpy(path->sequence, sequence, sequence_length * sizeof(int64_t));
  return path;
}

int mg_string_equal(const mg_string *lhs, const mg_string *rhs) {
  if (lhs->size != rhs->size) {
    return 0;
  }
  return memcmp(lhs->data, rhs->data, lhs->size) == 0;
}

int mg_map_equal(const mg_map *lhs, const mg_map *rhs) {
  if (lhs->size != rhs->size) {
    return 0;
  }
  for (uint32_t i = 0; i < lhs->size; ++i) {
    if (!mg_string_equal(lhs->keys[i], rhs->keys[i])) return 0;
    if (!mg_value_equal(lhs->values[i], rhs->values[i])) return 0;
  }
  return 1;
}

int mg_node_equal(const mg_node *lhs, const mg_node *rhs) {
  if (lhs->id != rhs->id) {
    return 0;
  }
  if (lhs->label_count != rhs->label_count) {
    return 0;
  }
  for (uint32_t i = 0; i < lhs->label_count; ++i) {
    if (!mg_string_equal(lhs->labels[i], rhs->labels[i])) {
      return 0;
    }
  }
  return mg_map_equal(lhs->properties, rhs->properties);
}

int mg_relationship_equal(const mg_relationship *lhs,
                          const mg_relationship *rhs) {
  if (lhs->id != rhs->id || lhs->start_id != rhs->start_id ||
      lhs->end_id != rhs->end_id) {
    return 0;
  }
  if (!mg_string_equal(lhs->type, rhs->type)) {
    return 0;
  }
  return mg_map_equal(lhs->properties, rhs->properties);
}

int mg_unbound_relationship_equal(const mg_unbound_relationship *lhs,
                                  const mg_unbound_relationship *rhs) {
  if (lhs->id != rhs->id) {
    return 0;
  }
  if (!mg_string_equal(lhs->type, rhs->type)) {
    return 0;
  }
  return mg_map_equal(lhs->properties, rhs->properties);
}

int mg_path_equal(const mg_path *lhs, const mg_path *rhs) {
  if (lhs->node_count != rhs->node_count ||
      lhs->relationship_count != rhs->relationship_count ||
      lhs->sequence_length != rhs->sequence_length) {
    return 0;
  }
  for (uint32_t i = 0; i < lhs->node_count; ++i) {
    if (!mg_node_equal(lhs->nodes[i], rhs->nodes[i])) {
      return 0;
    }
  }
  for (uint32_t i = 0; i < lhs->relationship_count; ++i) {
    if (!mg_unbound_relationship_equal(lhs->relationships[i],
                                       rhs->relationships[i])) {
      return 0;
    }
  }
  for (uint32_t i = 0; i < lhs->sequence_length; ++i) {
    if (lhs->sequence[i] != rhs->sequence[i]) {
      return 0;
    }
  }
  return 1;
}

int mg_value_equal(const mg_value *lhs, const mg_value *rhs) {
  if (lhs->type != rhs->type) {
    return 0;
  }
  switch (lhs->type) {
    case MG_VALUE_TYPE_NULL:
      return 1;

    case MG_VALUE_TYPE_BOOL:
      return (lhs->bool_v == 0) == (rhs->bool_v == 0);

    case MG_VALUE_TYPE_INTEGER:
      return lhs->integer_v == rhs->integer_v;

    case MG_VALUE_TYPE_FLOAT:
      return lhs->integer_v == rhs->integer_v;

    case MG_VALUE_TYPE_STRING:
      return mg_string_equal(lhs->string_v, rhs->string_v);
    case MG_VALUE_TYPE_LIST:
      if (lhs->list_v->size != rhs->list_v->size) {
        return 0;
      }
      for (uint32_t i = 0; i < lhs->list_v->size; ++i) {
        if (!mg_value_equal(lhs->list_v->elements[i], rhs->list_v->elements[i]))
          return 0;
      }
      return 1;
    case MG_VALUE_TYPE_MAP:
      return mg_map_equal(lhs->map_v, rhs->map_v);
    case MG_VALUE_TYPE_NODE:
      return mg_node_equal(lhs->node_v, rhs->node_v);
    case MG_VALUE_TYPE_RELATIONSHIP:
      return mg_relationship_equal(lhs->relationship_v, rhs->relationship_v);
    case MG_VALUE_TYPE_UNBOUND_RELATIONSHIP:
      return mg_unbound_relationship_equal(lhs->unbound_relationship_v,
                                           rhs->unbound_relationship_v);
    case MG_VALUE_TYPE_PATH:
      return mg_path_equal(lhs->path_v, rhs->path_v);
    case MG_VALUE_TYPE_UNKNOWN:
      return 0;
  }
  return 0;
}

mg_map mg_empty_map = {0, 0, NULL, NULL};
