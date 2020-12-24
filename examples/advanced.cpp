#include <iostream>
#include <numeric>

#include "mgclient.hpp"

void ClearDatabaseData(mg::Client *client) {
  if (!client->Execute("MATCH (n) DETACH DELETE n;")) {
    std::cerr << "Failed to delete all data from the database." << std::endl;
    std::exit(1);
  }
  client->DiscardAll();
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " [host] [port]\n";
    std::exit(1);
  }

  mg::Client::Init();

  {
    mg::Client::Params params;
    params.host = argv[1];
    params.port = static_cast<uint16_t>(atoi(argv[2]));
    auto client = mg::Client::Connect(params);
    if (!client) {
      std::cerr << "Failed to connect." << std::endl;
      return 1;
    }

    ClearDatabaseData(client.get());

    if (!client->Execute("CREATE INDEX ON :Person(id);")) {
      std::cerr << "Failed to create an index." << std::endl;
      return 1;
    }
    client->DiscardAll();

    if (!client->Execute(
            "CREATE (:Person:Entrepreneur {id: 0, age: 40, name: 'John', "
            "isStudent: false, score: 5.0});")) {
      std::cerr << "Failed to add data." << std::endl;
      return 1;
    }
    client->DiscardAll();

    if (!client->Execute("MATCH (n) RETURN n;")) {
      std::cerr << "Failed to read data." << std::endl;
      return 1;
    }
    while (const auto maybeResult = client->FetchOne()) {
      const auto result = *maybeResult;
      if (result.size() < 1) {
        continue;
      }
      const auto value = result[0];
      if (value.type() == mg::Value::Type::Node) {
        const auto node = value.ValueNode();
        auto labels = node.labels();
        std::string labels_str = std::accumulate(
            labels.begin(), labels.end(), std::string(""),
            [](const std::string &acc, const std::string_view value) {
              return acc + ":" + std::string(value);
            });
        const auto props = node.properties();
        std::string props_str =
            std::accumulate(props.begin(), props.end(), std::string("{"),
                            [](const std::string &acc, const auto &key_value) {
                              const auto &key = key_value.first;
                              const auto &value = key_value.second;
                              std::string value_str;
                              if (value.type() == mg::Value::Type::Int)
                                value_str = std::to_string(value.ValueInt());
                              if (value.type() == mg::Value::Type::String)
                                value_str = value.ValueString();
                              if (value.type() == mg::Value::Type::Bool)
                                value_str = std::to_string(value.ValueBool());
                              if (value.type() == mg::Value::Type::Double)
                                value_str = std::to_string(value.ValueDouble());
                              return acc + " " + std::string(key) + ": " +
                                     value_str;
                            }) +
            " }";
        std::cout << labels_str << " " << props_str << std::endl;
      }
    }

    ClearDatabaseData(client.get());
  }

  mg::Client::Finalize();

  return 0;
}
