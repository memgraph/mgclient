// Copyright (c) 2016-2020 Memgraph Ltd. [https://memgraph.com]
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "mgclient-value.hpp"
#include "mgclient.h"

namespace mg {
class MgException : public std::exception {
 public:
  explicit MgException(const std::string_view message) : msg_(message) {}

  const char *what() const noexcept override { return msg_.c_str(); }

 protected:
  std::string msg_;
};

class ClientException : public MgException {
 public:
  explicit ClientException(const std::string_view message)
      : MgException(message) {}
};

class TransientException : public MgException {
 public:
  explicit TransientException(const std::string_view message)
      : MgException(message) {}
};

class DatabaseException : public MgException {
 public:
  explicit DatabaseException(const std::string_view message)
      : MgException(message) {}
};

/// An interface for a Memgraph client that can execute queries and fetch
/// results.
class Client {
 public:
  struct Params {
    std::string host = "127.0.0.1";
    uint16_t port = 7687;
    std::string username = "";
    std::string password = "";
    bool use_ssl = false;
    std::string user_agent = "mgclient++/" + std::string(mg_client_version());
  };

  Client(const Client &) = delete;
  Client(Client &&) = default;
  Client &operator=(const Client &) = delete;
  Client &operator=(Client &&) = delete;
  ~Client();

  /// \brief Client software version.
  /// \return client version in the major.minor.patch format.
  static const char *Version();

  /// Initializes the client (the whole process).
  /// Should be called at the beginning of each process using the client.
  ///
  /// \return Zero if initialization was successful.
  static int Init();

  /// Finalizes the client (the whole process).
  /// Should be called at the end of each process using the client.
  static void Finalize();

  /// \brief Executes the given Cypher `statement`.
  /// \return true when the statement is successfully executed, false otherwise.
  /// \note
  /// After executing the statement, the method is blocked until all incoming
  /// data (execution results) are handled, i.e. until `FetchOne` method returns
  /// `std::nullopt`. Even if the result set is empty, the fetching has to be
  /// done/finished to be able to execute another statement.
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

  /// \brief Fetches all results and discards them.
  void DiscardAll();

  /// \brief Fetches all results.
  std::optional<std::vector<std::vector<Value>>> FetchAll();

  /// \brief Start a transaction.
  /// \return true when the transaction was successfully started, false
  /// otherwise.
  bool BeginTransaction();

  /// \brief Commit current transaction.
  /// \return true when the transaction was successfully committed, false
  /// otherwise.
  bool CommitTransaction();

  /// \brief Rollback current transaction.
  /// \return true when the transaction was successfully rollbacked, false
  /// otherwise.
  bool RollbackTransaction();

  /// \brief Static method that creates a Memgraph client instance.
  /// \return pointer to the created client instance.
  /// If the connection couldn't be established given the `params`, it returns
  /// a `nullptr`.
  static std::unique_ptr<Client> Connect(const Params &params);

 private:
  explicit Client(mg_session *session);

  mg_session *session_;
};

inline std::unique_ptr<Client> Client::Connect(const Client::Params &params) {
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
  mg_session_params_set_user_agent(mg_params, params.user_agent.c_str());
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

inline Client::Client(mg_session *session) : session_(session) {}

inline Client::~Client() { mg_session_destroy(session_); }

inline const char *Client::Version() { return mg_client_version(); }

inline int Client::Init() { return mg_init(); }

inline void Client::Finalize() { mg_finalize(); }

inline bool Client::Execute(const std::string &statement) {
  int status = mg_session_run(session_, statement.c_str(), nullptr, nullptr,
                              nullptr, nullptr);
  if (status < 0) {
    return false;
  }

  status = mg_session_pull(session_, nullptr);
  if (status < 0) {
    return false;
  }

  return true;
}

inline bool Client::Execute(const std::string &statement,
                            const ConstMap &params) {
  int status = mg_session_run(session_, statement.c_str(), params.ptr(),
                              nullptr, nullptr, nullptr);
  if (status < 0) {
    return false;
  }

  status = mg_session_pull(session_, nullptr);
  if (status < 0) {
    return false;
  }
  return true;
}

inline std::optional<std::vector<Value>> Client::FetchOne() {
  mg_result *result;
  int status = mg_session_fetch(session_, &result);
  if (status == MG_ERROR_CLIENT_ERROR) {
    throw ClientException(mg_session_error(session_));
  }

  if (status == MG_ERROR_TRANSIENT_ERROR) {
    throw TransientException(mg_session_error(session_));
  }

  if (status == MG_ERROR_DATABASE_ERROR) {
    throw DatabaseException(mg_session_error(session_));
  }

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

inline void Client::DiscardAll() {
  while (FetchOne())
    ;
}

inline std::optional<std::vector<std::vector<Value>>> Client::FetchAll() {
  std::vector<std::vector<Value>> data;
  while (auto maybe_result = FetchOne()) {
    data.emplace_back(std::move(*maybe_result));
  }
  return data;
}

inline bool Client::BeginTransaction() {
  return mg_session_begin_transaction(session_, nullptr) == 0;
}

inline bool Client::CommitTransaction() {
  mg_result *result;
  return mg_session_commit_transaction(session_, &result) == 0;
}

inline bool Client::RollbackTransaction() {
  mg_result *result;
  return mg_session_rollback_transaction(session_, &result) == 0;
}

}  // namespace mg
