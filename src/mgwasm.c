#include "mgwasm.h"

#ifdef __EMSCRIPTEN__

#include <stddef.h>
#include <stdio.h>
#include <sys/select.h>

int read_loop(const int sock) {
  fd_set fdr;
  FD_ZERO(&fdr);
  FD_SET(sock, &fdr);
  int poll = select(sock + 1, &fdr, NULL, NULL, NULL);
  if (poll == -1) {
    return -1;
  }
  if (!FD_ISSET(sock, &fdr)) {
    return -100;
  }
  return 1;
}

int write_loop(const int sock) {
  fd_set fdw;
  FD_ZERO(&fdw);
  FD_SET(sock, &fdw);
  int poll = select(sock + 1, NULL, &fdw, NULL, NULL);
  if (poll == -1) {
    return -1;
  }
  if (!FD_ISSET(sock, &fdw)) {
    return -100;
  }
  return 1;
}

static const size_t DELAY_MS = 10;

int mg_yield_until_async_read_sock(const int sock) {
  while (1) {
    emscripten_sleep(DELAY_MS);
    int res = read_loop(sock);
    if (res == 1) {
      return 1;
    }
    if (res == -1) {
      return -1;
    }
  }
}

int mg_yield_until_async_write_sock(const int sock) {
  while (1) {
    emscripten_sleep(DELAY_MS);
    int res = write_loop(sock);
    if (res == 1) {
      return 1;
    }
    if (res == -1) {
      return -1;
    }
  }
}

int mg_yield_until_async_read(const mg_transport *transport) {
  int sock = ((mg_raw_transport *)transport)->sockfd;
  return mg_yield_until_async_read_sock(sock);
}

int mg_yield_until_async_write(const mg_transport *transport) {
  int sock = ((mg_raw_transport *)transport)->sockfd;
  return mg_yield_until_async_write_sock(sock);
}

#endif
