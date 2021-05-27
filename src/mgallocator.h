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

#ifndef MGCLIENT_MGALLOCATOR_H
#define MGCLIENT_MGALLOCATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct mg_allocator {
  void *(*malloc)(struct mg_allocator *self, size_t size);
  void *(*realloc)(struct mg_allocator *self, void *buf, size_t size);
  void (*free)(struct mg_allocator *self, void *buf);
} mg_allocator;

void *mg_allocator_malloc(struct mg_allocator *allocator, size_t size);

void *mg_allocator_realloc(struct mg_allocator *allocator, void *buf,
                           size_t size);

void mg_allocator_free(struct mg_allocator *allocator, void *buf);

/// A special allocator used for decoding of Bolt messages
/// (more or less copied from libpq's PGResult allocator).
///
/// All decoded objects are released as soon as the next Bolt message is fetched
/// from the network buffer, so we don't need the ability to release them
/// individually. Therefore, instead of doing a single malloc for each decoded
/// value, memory is allocated in large blocks from the underlying allocator and
/// decoded values are placed inside those blocks. When there is no more
/// available space in the current block, a new one is allocated. This
/// should significantly reduce the amount of malloc calls done by the client.
//
/// Internally, `mg_linear_allocator` keeps a singly linked list of allocated
/// blocks. Only the head block is a candidate for inserting more data, any
/// extra space in other blocks is wasted. However, this is not too bad since
/// the wasted memory will be released as soon as the next message is fetched.
/// We could try to be smarter and iterate through all blocks to avoid wasting
/// memory but it is probably not worth it.
///
/// Memory returned by the allocator will be aligned as max_align_t.
///
/// Allocator is tuned with the following constructor parameters:
/// - block_size: size of the standard allocation block,
/// - sep_alloc_threshold: objects bigger that this size are given their
///                        separate blocks, as this will prevent wasting too
///                        much memory (the maximum amount of wasted memory per
///                        block is sep_alloc_threshold, not including padding).
///
/// When an objects gets its separate block, it will be placed as the second
/// element of the linked list, so we don't waste leftover space in the first
/// element.
///
/// Memory from the allocator is freed using `mg_linear_allocator_reset`. It
/// only keeps one empty block of size `block_size` to avoid allocating for each
/// row in the common case when the entire result row fits in a single block.
typedef struct mg_linear_allocator mg_linear_allocator;

mg_linear_allocator *mg_linear_allocator_init(mg_allocator *allocator,
                                              size_t block_size,
                                              size_t sep_alloc_threshold);

void mg_linear_allocator_reset(mg_linear_allocator *allocator);

void mg_linear_allocator_destroy(mg_linear_allocator *allocator);

extern struct mg_allocator mg_system_allocator;

#ifdef __cplusplus
}
#endif

#endif /* MGCLIENT_MGALLOCATOR_H */
