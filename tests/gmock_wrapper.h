#pragma once

// On older versions of clang/gcc deprecated-copy might not exist

#pragma GCC diagnostic push

#if (defined(__clang_major__) && __clang_major__ >= 10 && \
     !defined(__APPLE__)) ||                              \
    (defined(__GNUC__) && __GNUC__ >= 10)
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#endif

#include <gmock/gmock.h>

#pragma GCC diagnostic pop
