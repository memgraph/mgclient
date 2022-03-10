#ifndef MGCLIENT_MGWASM_H
#define MGCLIENT_MGWASM_H

#ifdef __EMSCRIPTEN__

#include "emscripten.h"
#include "mgtransport.h"

int read_loop(int sock);
int write_loop(int sock);

int mg_yield_until_async_read_sock(int sock);
int mg_yield_until_async_write_sock(int sock);

int mg_yield_until_async_read(const mg_transport *transport);
int mg_yield_until_async_write(const mg_transport *transport);

#endif

#endif /* MGCLIENT_MGWASM_H */
