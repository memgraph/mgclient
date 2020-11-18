#include <cstdlib>
#include <iostream>

#include <mgclient.hpp>

int main(int argc, char *argv[]) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " [host] [port] [query]\n";
    exit(1);
  }

  mg::Client::Init();

  std::cout << "mgclient version: " << mg::Client::Version() << std::endl;
  mg::Client::Params params;
  params.host = argv[1];
  params.port = static_cast<uint16_t>(atoi(argv[2]));
  params.use_ssl = false;
  auto client = mg::Client::Connect(params);

  if (!client) {
    std::cerr << "Failed to connect!\n";
    return 1;
  }

  if (!client->Execute(argv[3])) {
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
