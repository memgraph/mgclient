#include "mgwasm.h"

#include <stddef.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "emscripten.h"

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
  const int poll = select(sock + 1, NULL, &fdw, NULL, NULL);
  if (poll == -1) {
    return -1;
  }
  int result;
  socklen_t result_len = sizeof(result);
  if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0) {
    return -1;
  }

  if (result != 0) {
    return -1;
  }

  if (!FD_ISSET(sock, &fdw)) {
    return -100;
  }
  return 1;
}

static const size_t DELAY_MS = 10;

int mg_wasm_suspend_until_ready_to_read(const int sock) {
  while (1) {
    const int res = read_loop(sock);
    if (res == 1 || res == -1) {
      return res;
    }
    emscripten_sleep(DELAY_MS);
  }
}

int mg_wasm_suspend_until_ready_to_write(const int sock) {
  while (1) {
    const int res = write_loop(sock);
    if (res == 1 || res == -1) {
      return res;
    }
    if (res == -1) {
      return -1;
    }
    emscripten_sleep(DELAY_MS);
  }
}
