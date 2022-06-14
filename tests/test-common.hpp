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

#include <cassert>
#include <set>

#include "mgallocator.h"

#define ASSERT_READ_RAW(sstr, data)  \
  do {                               \
    SCOPED_TRACE("ASSERT_READ_RAW"); \
    AssertReadRaw((sstr), (data));   \
    ASSERT_NO_FATAL_FAILURE();       \
  } while (0)

#define ASSERT_READ_MESSAGE(sstr, expected) \
  do {                                      \
    SCOPED_TRACE("ASSERT_READ_MESSAGE");    \
    AssertReadMessage((sstr), (expected));  \
    ASSERT_NO_FATAL_FAILURE();              \
  } while (0)

#define ASSERT_END(sstr)        \
  do {                          \
    SCOPED_TRACE("ASSERT_END"); \
    AssertEnd((sstr));          \
  } while (0)

#define ASSERT_MEMORY_OK()                    \
  do {                                        \
    SCOPED_TRACE("ASSERT_MEMORY_OK");         \
    ASSERT_TRUE(allocator.allocated.empty()); \
  } while (0)

void *tracking_allocator_malloc(mg_allocator *allocator, size_t size);
void *tracking_allocator_realloc(mg_allocator *allocator, void *buf,
                                 size_t size);
void tracking_allocator_free(mg_allocator *allocator, void *buf);

struct tracking_allocator {
  void *(*malloc)(struct mg_allocator *self,
                  size_t size) = tracking_allocator_malloc;
  void *(*realloc)(struct mg_allocator *self, void *buf,
                   size_t size) = tracking_allocator_realloc;
  void (*free)(struct mg_allocator *self, void *buf) = tracking_allocator_free;
  std::set<void *> allocated;
};

void *tracking_allocator_malloc(mg_allocator *allocator, size_t size) {
  std::set<void *> *allocated = &((tracking_allocator *)allocator)->allocated;
  void *buf = malloc(size);
  if (buf != nullptr) {
    allocated->insert(buf);
  }
  return buf;
}

void *tracking_allocator_realloc(mg_allocator *allocator, void *buf,
                                 size_t size) {
  std::set<void *> *allocated = &((tracking_allocator *)allocator)->allocated;
  assert(size > 0);
  if (buf == nullptr) {
    return tracking_allocator_malloc(allocator, size);
  } else {
    auto it = allocated->find(buf);
    assert(it != allocated->end());
    allocated->erase(it);
    void *new_buf = realloc(buf, size);
    if (new_buf != nullptr) {
      allocated->insert(new_buf);
    }
    return new_buf;
  }
}

void tracking_allocator_free(mg_allocator *allocator, void *buf) {
  std::set<void *> *allocated = &((tracking_allocator *)allocator)->allocated;
  if (buf != nullptr) {
    auto it = allocated->find(buf);
    assert(it != allocated->end());
    allocated->erase(it);
  }
  free(buf);
}
