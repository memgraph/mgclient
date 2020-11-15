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

#ifndef MGCLIENT_MGCOMMON_H
#define MGCLIENT_MGCOMMON_H

#ifdef MGCLIENT_ON_LINUX

#include <endian.h>

#define MG_RETRY_ON_EINTR(expression)          \
  ({                                           \
    long result;                               \
    do {                                       \
      result = (long)(expression);             \
    } while (result == -1L && errno == EINTR); \
    result;                                    \
  })

#endif  // MGCLIENT_ON_LINUX

#ifdef MGCLIENT_ON_WINDOWS

// Based on https://gist.github.com/PkmX/63dd23f28ba885be53a5

#define htobe16(x) __builtin_bswap16(x)
#define htole16(x) (x)
#define be16toh(x) __builtin_bswap16(x)
#define le16toh(x) (x)

#define htobe32(x) __builtin_bswap32(x)
#define htole32(x) (x)
#define be32toh(x) __builtin_bswap32(x)
#define le32toh(x) (x)

#define htobe64(x) __builtin_bswap64(x)
#define htole64(x) (x)
#define be64toh(x) __builtin_bswap64(x)
#define le64toh(x) (x)

#endif  // MGCLIENT_ON_WINDOWS

#define MG_RETURN_IF_FAILED(expression) \
  do {                                  \
    int status = (expression);          \
    if (status != 0) {                  \
      return status;                    \
    }                                   \
  } while (0)

#endif
