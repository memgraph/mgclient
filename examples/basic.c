#include <stdio.h>
#include <stdlib.h>

#include <mgclient.h>

// Reads the environment variable `name`, falling back to `default_value` when
// it is unset. The C counterpart of the GetEnvOrDefault helper used by the
// integration tests, so the examples honor the same MEMGRAPH_HOST /
// MEMGRAPH_PORT overrides, e.g.
//   MEMGRAPH_HOST=<ip> MEMGRAPH_PORT=<port> ./example_basic_c "RETURN 1"
static const char *get_env_or_default(const char *name,
                                      const char *default_value) {
  const char *value = getenv(name);
  return value ? value : default_value;
}

static int get_env_int_or_default(const char *name, int default_value) {
  const char *value = getenv(name);
  return value ? atoi(value) : default_value;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s [query]\n", argv[0]);
    exit(1);
  }

  mg_init();
  printf("mgclient version: %s\n", mg_client_version());

  mg_session_params *params = mg_session_params_make();
  if (!params) {
    fprintf(stderr, "failed to allocate session parameters\n");
    exit(1);
  }
  const char *host = get_env_or_default("MEMGRAPH_HOST", "127.0.0.1");
  int port = get_env_int_or_default("MEMGRAPH_PORT", 7687);
  mg_session_params_set_host(params, host);
  mg_session_params_set_port(params, (uint16_t)port);
  mg_session_params_set_sslmode(params, MG_SSLMODE_DISABLE);

  mg_session *session = NULL;
  int status = mg_connect(params, &session);
  mg_session_params_destroy(params);
  if (status < 0) {
    printf("failed to connect to Memgraph: %s\n", mg_session_error(session));
    mg_session_destroy(session);
    return 1;
  }

  if (mg_session_run(session, argv[1], NULL, NULL, NULL, NULL) < 0) {
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
  mg_finalize();

  return 0;
}
