#include <stdio.h>
#include <stdlib.h>

#include <mgclient.h>

int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s [host] [port] [query]\n", argv[0]);
    exit(1);
  }

  printf("mgclient version: %s\n", mg_client_get_version());

  mg_session_params *params = mg_session_params_make();
  if (!params) {
    fprintf(stderr, "failed to allocate session parameters\n");
    exit(1);
  }
  mg_session_params_set_host(params, argv[1]);
  mg_session_params_set_port(params, (uint16_t)atoi(argv[2]));
  mg_session_params_set_sslmode(params, MG_SSLMODE_DISABLE);

  mg_session *session = NULL;
  int status = mg_connect(params, &session);
  mg_session_params_destroy(params);
  if (status < 0) {
    printf("failed to connect to Memgraph: %s\n", mg_session_error(session));
    mg_session_destroy(session);
    return 1;
  }

  if (mg_session_run(session, argv[3], NULL, NULL, NULL, NULL) < 0) {
    printf("failed to execute query: %s\n", mg_session_error(session));
    mg_session_destroy(session);
    return 1;
  }

  if (mg_session_pull(session, NULL)) {
    printf("failed to pull results of the query: %s\n",
           mg_session_error(session));
    mg_session_destroy(session);
    return 1;
  }

  mg_result *result;
  int rows = 0;
  while ((status = mg_session_fetch(session, &result)) == 1) {
    rows++;
  }

  if (status < 0) {
    printf("error occurred during query execution: %s\n",
           mg_session_error(session));
  } else {
    printf("query executed successfuly and returned %d rows\n", rows);
  }

  mg_session_destroy(session);
  return 0;
}
