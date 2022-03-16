#ifndef MGCLIENT_MGWASM_H
#define MGCLIENT_MGWASM_H

int mg_wasm_suspend_until_ready_to_read(int sock);
int mg_wasm_suspend_until_ready_to_write(int sock);

#endif /* MGCLIENT_MGWASM_H */
