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

#include "mgallocator.h"

#include "test-common.hpp"

using namespace std::string_literals;

TEST(LinearAllocatorTest, Basic) {
  tracking_allocator underlying_allocator;
  mg_linear_allocator *allocator = mg_linear_allocator_init(
      (mg_allocator *)&underlying_allocator, 4096, 2048);

  ASSERT_EQ(underlying_allocator.allocated.size(), 2u);

  for (int i = 0; i < 4; ++i) {
    mg_allocator_malloc((mg_allocator *)allocator, 1024);
  }
  ASSERT_EQ(underlying_allocator.allocated.size(), 2u);

  mg_allocator_malloc((mg_allocator *)allocator, 1024);
  ASSERT_EQ(underlying_allocator.allocated.size(), 3u);

  mg_linear_allocator_reset(allocator);
  ASSERT_EQ(underlying_allocator.allocated.size(), 2u);

  for (int i = 0; i < 4; ++i) {
    mg_allocator_malloc((mg_allocator *)allocator, 1024);
  }
  ASSERT_EQ(underlying_allocator.allocated.size(), 2u);

  mg_linear_allocator_destroy(allocator);
  ASSERT_EQ(underlying_allocator.allocated.size(), 0u);
}

TEST(LinearAllocatorTest, SeparateAllocations) {
  tracking_allocator underlying_allocator;
  mg_linear_allocator *allocator = mg_linear_allocator_init(
      (mg_allocator *)&underlying_allocator, 4096, 2048);

  ASSERT_EQ(underlying_allocator.allocated.size(), 2u);

  for (int i = 0; i < 3; ++i) {
    mg_allocator_malloc((mg_allocator *)allocator, 1024);
  }
  ASSERT_EQ(underlying_allocator.allocated.size(), 2u);

  mg_allocator_malloc((mg_allocator *)allocator, 2048);
  ASSERT_EQ(underlying_allocator.allocated.size(), 3u);

  mg_allocator_malloc((mg_allocator *)allocator, 1024);
  ASSERT_EQ(underlying_allocator.allocated.size(), 3u);

  mg_allocator_malloc((mg_allocator *)allocator, 1024);
  ASSERT_EQ(underlying_allocator.allocated.size(), 4u);

  mg_linear_allocator_reset(allocator);
  ASSERT_EQ(underlying_allocator.allocated.size(), 2u);

  for (int i = 0; i < 4; ++i) {
    mg_allocator_malloc((mg_allocator *)allocator, 1024);
  }
  ASSERT_EQ(underlying_allocator.allocated.size(), 2u);

  mg_linear_allocator_destroy(allocator);
  ASSERT_EQ(underlying_allocator.allocated.size(), 0u);
}
