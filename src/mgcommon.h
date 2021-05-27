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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MGCLIENT_ON_APPLE
#include "apple/mgcommon.h"
#endif  // MGCLIENT_ON_APPLE

#ifdef MGCLIENT_ON_LINUX
#include "linux/mgcommon.h"
#endif  // MGCLIENT_ON_LINUX

#ifdef MGCLIENT_ON_WINDOWS
#include "windows/mgcommon.h"
#endif  // MGCLIENT_ON_WINDOWS

#define MG_RETURN_IF_FAILED(expression) \
  do {                                  \
    int status = (expression);          \
    if (status != 0) {                  \
      return status;                    \
    }                                   \
  } while (0)

#ifdef NDEBUG
#define DB_ACTIVE 0
#else
#define DB_ACTIVE 1
#endif  // NDEBUG
#define DB_LOG(x)                      \
  do {                                 \
    if (DB_ACTIVE) fprintf(stderr, x); \
  } while (0)

#ifdef MGCLIENT_ON_APPLE
#define MG_ATTRIBUTE_WEAK __attribute__((weak))
#else
#define MG_ATTRIBUTE_WEAK
#endif

#ifdef __cplusplus
}
#endif

#endif /* MGCLIENT_MGCOMMON_H */
