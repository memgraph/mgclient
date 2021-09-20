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

#include "mgallocator.h"

#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
typedef double max_align_t;
#endif

void *mg_system_realloc(struct mg_allocator *self, void *buf, size_t size) {
  (void)self;
  return realloc(buf, size);
}

void *mg_system_malloc(struct mg_allocator *self, size_t size) {
  (void)self;
  return malloc(size);
}

void mg_system_free(struct mg_allocator *self, void *buf) {
  (void)self;
  free(buf);
}

void *mg_allocator_malloc(struct mg_allocator *allocator, size_t size) {
  return allocator->malloc(allocator, size);
}

void *mg_allocator_realloc(struct mg_allocator *allocator, void *buf,
                           size_t size) {
  return allocator->realloc(allocator, buf, size);
}

void mg_allocator_free(struct mg_allocator *allocator, void *buf) {
  allocator->free(allocator, buf);
}

struct mg_allocator mg_system_allocator = {mg_system_malloc, mg_system_realloc,
                                           mg_system_free};

typedef struct mg_memory_block {
  char *buffer;
  struct mg_memory_block *next;
} mg_memory_block;

mg_memory_block *mg_memory_block_alloc(mg_allocator *allocator, size_t size) {
  /// Ensure that the memory is properly aligned.
  _Static_assert(
      sizeof(mg_memory_block) % alignof(max_align_t) == 0,
      "Size of mg_memory_block doesn't satisfy alignment requirements");
  mg_memory_block *block =
      mg_allocator_malloc(allocator, sizeof(mg_memory_block) + size);
  if (!block) {
    return NULL;
  }
  block->next = NULL;
  block->buffer = (char *)block + sizeof(mg_memory_block);
  return block;
}

typedef struct mg_linear_allocator {
  void *(*malloc)(struct mg_allocator *self, size_t size);
  void *(*realloc)(struct mg_allocator *self, void *buf, size_t size);
  void (*free)(struct mg_allocator *self, void *buf);

  mg_memory_block *current_block;
  size_t current_offset;

  const size_t block_size;
  const size_t sep_alloc_threshold;

  mg_allocator *underlying_allocator;
} mg_linear_allocator;

void *mg_linear_allocator_malloc(struct mg_allocator *allocator, size_t size) {
  mg_linear_allocator *self = (mg_linear_allocator *)allocator;

  if (size >= self->sep_alloc_threshold) {
    // Make a new block, but put it below the first block so we don't waste
    // bytes in it.
    mg_memory_block *new_block =
        mg_memory_block_alloc(self->underlying_allocator, size);
    new_block->next = self->current_block->next;
    self->current_block->next = new_block;
    return new_block->buffer;
  }

  if (self->current_offset + size > self->block_size) {
    // Create a new block and put it at the beginning of the list.
    mg_memory_block *new_block =
        mg_memory_block_alloc(self->underlying_allocator, self->block_size);
    new_block->next = self->current_block;
    self->current_block = new_block;
    self->current_offset = 0;
  }

  assert(self->current_offset + size <= self->block_size);
  assert(self->current_offset % alignof(max_align_t) == 0);

  void *ret = self->current_block->buffer + self->current_offset;
  self->current_offset += size;
  if (self->current_offset % alignof(max_align_t) != 0) {
    self->current_offset +=
        alignof(max_align_t) - (self->current_offset % alignof(max_align_t));
  }
  return ret;
}

void *mg_linear_allocator_realloc(struct mg_allocator *allocator, void *buf,
                                  size_t size) {
  (void)allocator;
  (void)buf;
  (void)size;
  fprintf(stderr, "mg_linear_allocator doesn't support realloc\n");
  return NULL;
}

void mg_linear_allocator_free(struct mg_allocator *allocator, void *buf) {
  (void)allocator;
  (void)buf;
  return;
}

mg_linear_allocator *mg_linear_allocator_init(mg_allocator *allocator,
                                              size_t block_size,
                                              size_t sep_alloc_threshold) {
  mg_memory_block *first_block = mg_memory_block_alloc(allocator, block_size);
  if (!first_block) {
    return NULL;
  }

  mg_linear_allocator tmp_alloc = {mg_linear_allocator_malloc,
                                   mg_linear_allocator_realloc,
                                   mg_linear_allocator_free,
                                   first_block,
                                   0,
                                   block_size,
                                   sep_alloc_threshold,
                                   allocator};
  mg_linear_allocator *alloc =
      mg_allocator_malloc(allocator, sizeof(mg_linear_allocator));
  if (!alloc) {
    mg_allocator_free(allocator, first_block);
    return NULL;
  }

  memcpy(alloc, &tmp_alloc, sizeof(mg_linear_allocator));
  return alloc;
}

void mg_linear_allocator_destroy(mg_linear_allocator *allocator) {
  if (allocator == NULL) {
    return;
  }
  while (allocator->current_block) {
    mg_memory_block *next_block = allocator->current_block->next;
    mg_allocator_free(allocator->underlying_allocator,
                      allocator->current_block);
    allocator->current_block = next_block;
  }
  mg_allocator_free(allocator->underlying_allocator, allocator);
}

void mg_linear_allocator_reset(mg_linear_allocator *allocator) {
  // The first block is always of size allocator->block_size. We will keep that
  // one and free the others.
  mg_memory_block *first_block = allocator->current_block;
  while (first_block->next) {
    mg_memory_block *new_next = first_block->next->next;
    mg_allocator_free(allocator->underlying_allocator, first_block->next);
    first_block->next = new_next;
  }
  allocator->current_offset = 0;
}
