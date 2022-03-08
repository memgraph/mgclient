#include "mgwasm.h"

#ifdef __EMSCRIPTEN__

#include <stddef.h>
#include <stdio.h>
#include <sys/select.h>

int read_loop(int sock) {
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

int write_loop(int sock) {
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

int yield_until_async_read(int sock, int ms) {
  while (1) {
    emscripten_sleep(ms);
    int res = read_loop(sock);
    if (res == 1) {
      return 1;
    }
    if (res == -1) {
      return -1;
    }
  }
}

int yield_until_async_write(int sock, int ms) {
  while (1) {
    emscripten_sleep(ms);
    int res = write_loop(sock);
    if (res == 1) {
      return 1;
    }
    if (res == -1) {
      return -1;
    }
  }
}

#endif
