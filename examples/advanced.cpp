#include <cstdlib>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>

#include "mgclient.hpp"

// Reads the environment variable `value_name`, falling back to `default_value`
// when it is unset. Matches the helper used by the integration tests so the
// examples honor the same MEMGRAPH_HOST / MEMGRAPH_PORT overrides, e.g.
//   MEMGRAPH_HOST=<ip> MEMGRAPH_PORT=<port> ./example_advanced_cpp
template <typename T>
T GetEnvOrDefault(const std::string &value_name, const T &default_value) {
  const char *char_value = std::getenv(value_name.c_str());
  if (!char_value) return default_value;
  T value;
  std::stringstream env_value_stream(char_value);
  env_value_stream >> value;
  return value;
}

void ClearDatabaseData(mg::Client *client) {
  if (!client->Execute("MATCH (n) DETACH DELETE n;")) {
    std::cerr << "Failed to delete all data from the database." << std::endl;
    std::exit(1);
  }
  client->DiscardAll();
}

std::string MgValueToString(const mg::ConstValue &value) {
  std::string value_str = "";
  if (value.type() == mg::Value::Type::Int) {
    value_str = std::to_string(value.ValueInt());
  } else if (value.type() == mg::Value::Type::String) {
    value_str = value.ValueString();
  } else if (value.type() == mg::Value::Type::Bool) {
    value_str = std::to_string(value.ValueBool());
  } else if (value.type() == mg::Value::Type::Double) {
    value_str = std::to_string(value.ValueDouble());
  } else if (value.type() == mg::Value::Type::Point2d) {
    auto point2d = value.ValuePoint2d();
    value_str += "Point2D({ srid:" + std::to_string(point2d.srid()) +
                 ", x:" + std::to_string(point2d.x()) +
                 ", y:" + std::to_string(point2d.y()) + " })";
  } else if (value.type() == mg::Value::Type::Point3d) {
    auto point3d = value.ValuePoint3d();
    value_str += "Point3D({ srid:" + std::to_string(point3d.srid()) +
                 ", x:" + std::to_string(point3d.x()) +
                 ", y:" + std::to_string(point3d.y()) +
                 ", z:" + std::to_string(point3d.z()) + " })";
  } else if (value.type() == mg::Value::Type::List) {
    value_str += "[";
    for (auto item : value.ValueList()) {
      value_str += MgValueToString(item) + ",";
    }
    value_str += "]";
  } else {
    std::cerr << "Uncovered converstion from data type to a string"
              << std::endl;
    std::exit(1);
  }
  return value_str;
}

int main() {
  mg::Client::Init();

  {
    mg::Client::Params params;
    params.host = GetEnvOrDefault<std::string>("MEMGRAPH_HOST", "127.0.0.1");
    params.port = GetEnvOrDefault<uint16_t>("MEMGRAPH_PORT", 7687);
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
            "isStudent: false, score: 5.0, "
            "position2D: point({x: 1, y: 2, srid: 4326}), "
            "position3D: point({x: 8, y: 9, z: 10, srid: 9757}) });")) {
      std::cerr << "Failed to add data." << std::endl;
      return 1;
    }
    client->DiscardAll();

    if (!client->Execute("MATCH (n) RETURN n;")) {
      std::cerr << "Failed to read data." << std::endl;
      return 1;
    }
    if (const auto maybe_data = client->FetchAll()) {
      const auto data = *maybe_data;
      std::cout << "Number of results: " << data.size() << std::endl;
    }

    mg::Map query_params(2);
    query_params.Insert("id", mg::Value(0));
    mg::List list_param(std::vector<mg::Value>{mg::Value(1), mg::Value(1)});
    query_params.Insert("list", mg::Value(std::move(list_param)));
    if (!client->Execute("CREATE (n {id: $id, list: $list}) RETURN n;",
                         query_params.AsConstMap())) {
      std::cerr << "Failed to read data by parametrized query." << std::endl;
      return 1;
    }
    if (const auto maybe_data = client->FetchAll()) {
      const auto data = *maybe_data;
      std::cout << "Number of results: " << data.size() << std::endl;
    }

    if (!client->Execute("MATCH (n) RETURN n;")) {
      std::cerr << "Failed to read data." << std::endl;
      return 1;
    }
    while (const auto maybe_result = client->FetchOne()) {
      const auto result = *maybe_result;
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
                              const auto &[key, value] = key_value;
                              return acc + " " + std::string(key) + ": " +
                                     MgValueToString(value);
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
