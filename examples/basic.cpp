#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include <mgclient.hpp>

// Reads the environment variable `value_name`, falling back to `default_value`
// when it is unset. Matches the helper used by the integration tests so the
// examples honor the same MEMGRAPH_HOST / MEMGRAPH_PORT overrides, e.g.
//   MEMGRAPH_HOST=<ip> MEMGRAPH_PORT=<port> ./example_basic_cpp "RETURN 1"
template <typename T>
T GetEnvOrDefault(const std::string &value_name, const T &default_value) {
  const char *char_value = std::getenv(value_name.c_str());
  if (!char_value) return default_value;
  T value;
  std::stringstream env_value_stream(char_value);
  env_value_stream >> value;
  return value;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " [query]\n";
    exit(1);
  }

  mg::Client::Init();

  std::cout << "mgclient version: " << mg::Client::Version() << std::endl;
  mg::Client::Params params;
  params.host = GetEnvOrDefault<std::string>("MEMGRAPH_HOST", "127.0.0.1");
  params.port = GetEnvOrDefault<uint16_t>("MEMGRAPH_PORT", 7687);
  params.use_ssl = false;
  auto client = mg::Client::Connect(params);

  if (!client) {
    std::cerr << "Failed to connect!\n";
    return 1;
  }

  if (!client->Execute(argv[1])) {
    std::cerr << "Failed to execute query!";
    return 1;
  }

  int rows = 0;
  while (const auto maybeResult = client->FetchOne()) {
    ++rows;
  }

  std::cout << "Fetched " << rows << " row(s)\n";

  // Deallocate the client because mg_finalize has to be called globally.
  client.reset(nullptr);

  mg::Client::Finalize();

  return 0;
}
