#ifndef MGCLIENT_MGWASM_UTIL_H
#define MGCLIENT_MGWASM_UTIL_H

#ifdef __EMSCRIPTEN__

#include "emscripten.h"

int read_loop(int sock);
int write_loop(int sock);

int yield_until_async_read(int sock, int ms);
int yield_until_async_write(int sock, int ms);

#endif

#endif
