#pragma once

#include <memory>
#include <optional>

#include "mgclient.h"
#include "mgvalue.hpp"

namespace mg {

/// An interface for a Memgraph client that can execute queries and fetch
/// results.
class Client {
 public:
  struct Params {
    std::string host;
    uint16_t port;
    std::string username;
    std::string password;
    bool use_ssl;
    std::string client_name;
  };

  Client(const Client &) = delete;
  Client(Client &&) = default;
  Client &operator=(const Client &) = delete;
  Client &operator=(Client &&) = delete;
  ~Client();

  /// \brief Executes the given Cypher `statement`.
  /// \return true when the statement is successfully executed, false otherwise.
  /// \note
  /// After executing the statement, the method is blocked until all incoming
  /// data (execution results) are handled, i.e. until `FetchOne` method returns
  /// `std::nullopt`.
  bool Execute(const std::string &statement);

  /// \brief Executes the given Cypher `statement`, supplied with additional
  /// `params`.
  /// \return true when the statement is successfully executed, false
  /// otherwise.
  /// \note
  /// After executing the statement, the method is blocked
  /// until all incoming data (execution results) are handled, i.e. until
  /// `FetchOne` method returns `std::nullopt`.
  bool Execute(const std::string &statement, const ConstMap &params);

  /// \brief Fetches the next result from the input stream.
  /// \return next result from the input stream.
  /// If there is nothing to fetch, `std::nullopt` is returned.
  std::optional<std::vector<Value>> FetchOne();

  /// \brief Static method that creates a Memgraph client instance.
  /// \return pointer to the created client instance.
  /// If the connection couldn't be established given the `params`, it returns
  /// a `nullptr`.
  static std::unique_ptr<Client> Connect(const Params &params);

 private:
  explicit Client(mg_session *session);

  mg_session *session_;
};

std::unique_ptr<Client> Client::Connect(const Client::Params &params) {
  mg_session_params *mg_params = mg_session_params_make();
  if (!mg_params) {
    return nullptr;
  }
  mg_session_params_set_host(mg_params, params.host.c_str());
  mg_session_params_set_port(mg_params, params.port);
  if (!params.username.empty()) {
    mg_session_params_set_username(mg_params, params.username.c_str());
    mg_session_params_set_password(mg_params, params.password.c_str());
  }
  mg_session_params_set_client_name(mg_params, params.client_name.c_str());
  mg_session_params_set_sslmode(
      mg_params, params.use_ssl ? MG_SSLMODE_REQUIRE : MG_SSLMODE_DISABLE);

  mg_session *session = nullptr;
  int status = mg_connect(mg_params, &session);
  mg_session_params_destroy(mg_params);
  if (status < 0) {
    return nullptr;
  }

  // Using `new` to access private constructor.
  return std::unique_ptr<Client>(new Client(session));
}

Client::Client(mg_session *session) : session_(session) {}

Client::~Client() { mg_session_destroy(session_); }

bool Client::Execute(const std::string &statement) {
  int status = mg_session_run(session_, statement.c_str(), nullptr, nullptr);
  if (status < 0) {
    return false;
  }
  return true;
}

bool Client::Execute(const std::string &statement, const ConstMap &params) {
  int status =
      mg_session_run(session_, statement.c_str(), params.ptr(), nullptr);
  if (status < 0) {
    return false;
  }
  return true;
}

std::optional<std::vector<Value>> Client::FetchOne() {
  mg_result *result;
  int status = mg_session_pull(session_, &result);
  if (status != 1) {
    return std::nullopt;
  }

  std::vector<Value> values;
  const mg_list *list = mg_result_row(result);
  const size_t list_length = mg_list_size(list);
  values.reserve(list_length);
  for (size_t i = 0; i < list_length; ++i) {
    values.emplace_back(Value(mg_list_at(list, i)));
  }
  return values;
}

}  // namespace mg
