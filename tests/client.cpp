// Copyright (c) 2016-2019 Memgraph Ltd. [https://memgraph.com]
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

#include <random>
#include <thread>

#include <arpa/inet.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "mgclient.h"

extern "C" {
#include "mgcommon.h"
#include "mgsession.h"
}

#include "test-common.hpp"

using namespace std::string_literals;
using ::testing::HasSubstr;

extern "C" {
char TEST_KEY_FP[] =
    "20cb119eb74dd28e8a4861f51dfc6d1d31306751bc3694470161b057e706f6c00cb4c0f994"
    "317f59e570b594cff7224c8699fa70a4b2f7ba1f99c2b8e092943b";

extern int mg_connect_ca(const mg_session_params *params, mg_session **session,
                         mg_allocator *allocator);

int trust_callback_ok = 0;
int trust_callback(const char *hostname, const char *ip_address,
                   const char *key_type, const char *fingerprint,
                   void *trust_data) {
  trust_callback_ok = 1;
  if (!hostname || strcmp(hostname, "localhost") != 0) {
    trust_callback_ok = 0;
  }
  if (!ip_address || strcmp(ip_address, "127.0.0.1") != 0) {
    trust_callback_ok = 0;
  }
  if (!key_type || strcmp(key_type, "rsaEncryption") != 0) {
    trust_callback_ok = 0;
  }
  if (!fingerprint || strcmp(fingerprint, TEST_KEY_FP) != 0) {
    trust_callback_ok = 0;
  }
  if (!trust_data || *(int *)trust_data != 42) {
    trust_callback_ok = 0;
  }
  return 0;
}
// HACK: We're mocking out the `mg_secure_transport_init` function to avoid the
// need of writing the server side SSL logic (all of that is tested in
// transport.cpp). We're just checking that the right parameters are passed and
// return a `test_transport` object that actually uses `mg_raw_transport`
// methods for writing and reading.
int mg_secure_transport_init_called = 0;

struct test_transport {
  int (*send)(struct mg_transport *, const char *buf, size_t len);
  int (*recv)(struct mg_transport *, char *buf, size_t len);
  void (*destroy)(struct mg_transport *);
  union {
    struct {
      SSL *ssl;
      BIO *bio;
    } padding;
    int sockfd;
  };
  const char *peer_pubkey_type;
  char *peer_pubkey_fp;
};

void test_transport_destroy(mg_transport *transport) { free(transport); }

int __wrap_mg_secure_transport_init(int sockfd, const char *cert,
                                    const char *key,
                                    mg_secure_transport **transport) {
  mg_secure_transport_init_called = 1;
  if (!cert || strcmp(cert, "/path/to/cert") != 0) {
    return MG_ERROR_UNKNOWN_ERROR;
  }
  if (!key || strcmp(key, "/path/to/key") != 0) {
    return MG_ERROR_UNKNOWN_ERROR;
  }
  test_transport *ttransport = (test_transport *)malloc(sizeof(test_transport));
  ttransport->sockfd = sockfd;
  ttransport->send = mg_raw_transport_send;
  ttransport->recv = mg_raw_transport_recv;
  ttransport->destroy = test_transport_destroy;
  ttransport->peer_pubkey_type = "rsaEncryption";
  ttransport->peer_pubkey_fp = TEST_KEY_FP;
  *transport = (mg_secure_transport *)ttransport;
  return 0;
}
}

// Helper function for reading/writing an entire block of data from/to socket.
int SendData(int sockfd, const char *buf, size_t len) {
  size_t total_sent = 0;
  while (total_sent < len) {
    ssize_t sent_now = MG_RETRY_ON_EINTR(
        send(sockfd, buf + total_sent, len - total_sent, MSG_NOSIGNAL));
    if (sent_now == -1) {
      return -1;
    }
    total_sent += (size_t)sent_now;
  }
  return 0;
}

int RecvData(int sockfd, char *buf, size_t len) {
  size_t total_received = 0;
  while (total_received < len) {
    ssize_t received_now = MG_RETRY_ON_EINTR(
        recv(sockfd, buf + total_received, len - total_received, 0));
    if (received_now == 0) {
      // Server closed the connection.
      return -1;
    }
    if (received_now == -1) {
      return -1;
    }
    total_received += (size_t)received_now;
  }
  return 0;
}

class ConnectTest : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    {
      struct sockaddr_in server_addr;
      server_addr.sin_family = AF_INET;
      int status =
          inet_pton(AF_INET, "127.0.0.1", (void *)&server_addr.sin_addr);
      if (status != 1) {
        if (status == 0) {
          fprintf(stderr, "invalid address\n");
        } else {
          fprintf(stderr, "%s\n", strerror(errno));
        }
        abort();
      }

      ss = socket(AF_INET, SOCK_STREAM, 0);
      if (ss < 0) {
        abort();
      }

      std::uniform_int_distribution<int> dist(10000, 20000);
      std::mt19937 gen(std::random_device{}());
      for (int i = 0; i < 20; ++i) {
        port = htons(dist(gen));
        server_addr.sin_port = htons((uint16_t)port);
        if (bind(ss, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
            0) {
          port = -1;
        } else {
          break;
        }
      }

      if (port == -1) {
        abort();
      }

      if (listen(ss, 50) != 0) {
        abort();
      }
    }
  }

  virtual void TearDown() override { close(ss); }

  void RunServer(const std::function<void(int)> &server_func) {
    server_thread = std::thread([this, server_func] {
      int sockfd = accept(ss, NULL, NULL);
      if (sockfd < 0) {
        abort();
      }
      server_func(sockfd);
    });
    server_thread.detach();
  }

  void StopServer() {
    if (server_thread.joinable()) {
      server_thread.join();
    }
  }

  int ss;
  int port;
  std::thread server_thread;

  tracking_allocator allocator;
};

TEST_F(ConnectTest, MissingHost) {
  mg_session_params *params = mg_session_params_make();
  mg_session_params_set_port(params, port);
  mg_session *session;
  ASSERT_EQ(mg_connect_ca(params, &session, (mg_allocator *)&allocator),
            MG_ERROR_BAD_PARAMETER);
  EXPECT_EQ(mg_session_status(session), MG_SESSION_BAD);
  mg_session_params_destroy(params);
  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(ConnectTest, InvalidHost) {
  mg_session_params *params = mg_session_params_make();
  mg_session_params_set_host(params, "285.42.1.34");
  mg_session_params_set_port(params, port);
  mg_session *session;
  ASSERT_EQ(mg_connect_ca(params, &session, (mg_allocator *)&allocator),
            MG_ERROR_NETWORK_FAILURE);
  EXPECT_EQ(mg_session_status(session), MG_SESSION_BAD);
  mg_session_params_destroy(params);
  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(ConnectTest, SSLCertWithoutKey) {
  mg_session_params *params = mg_session_params_make();
  mg_session_params_set_host(params, "127.0.0.1");
  mg_session_params_set_port(params, 12345);
  mg_session_params_set_sslmode(params, MG_SSLMODE_REQUIRE);
  mg_session_params_set_sslcert(params, "/path/to/cert");
  mg_session *session;
  ASSERT_EQ(mg_connect_ca(params, &session, (mg_allocator *)&allocator),
            MG_ERROR_BAD_PARAMETER);
  EXPECT_EQ(mg_session_status(session), MG_SESSION_BAD);
  mg_session_params_destroy(params);
  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(ConnectTest, HandshakeFail) {
  RunServer([](int sockfd) {
    char handshake[20];
    ASSERT_EQ(RecvData(sockfd, handshake, 20), 0);
    ASSERT_EQ(std::string(handshake, 4), "\x60\x60\xB0\x17"s);
    ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x00\x01"s);
    ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x00"s);
    ASSERT_EQ(std::string(handshake + 12, 4), "\x00\x00\x00\x00"s);
    ASSERT_EQ(std::string(handshake + 16, 4), "\x00\x00\x00\x00"s);

    // Send unsupported version to client.
    uint32_t version = htobe32(2);
    ASSERT_EQ(SendData(sockfd, (char *)&version, 4), 0);

    close(sockfd);
  });
  mg_session_params *params = mg_session_params_make();
  mg_session_params_set_host(params, "127.0.0.1");
  mg_session_params_set_port(params, port);
  mg_session *session;
  ASSERT_EQ(mg_connect_ca(params, &session, (mg_allocator *)&allocator),
            MG_ERROR_PROTOCOL_VIOLATION);
  EXPECT_EQ(mg_session_status(session), MG_SESSION_BAD);
  mg_session_params_destroy(params);
  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(ConnectTest, InitFail) {
  RunServer([](int sockfd) {
    // Perform handshake.
    {
      char handshake[20];
      ASSERT_EQ(RecvData(sockfd, handshake, 20), 0);
      ASSERT_EQ(std::string(handshake, 4), "\x60\x60\xB0\x17"s);
      ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x00\x01"s);
      ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 12, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 16, 4), "\x00\x00\x00\x00"s);

      uint32_t version = htobe32(1);
      ASSERT_EQ(SendData(sockfd, (char *)&version, 4), 0);
    }

    mg_session *session = mg_session_init(&mg_system_allocator);
    ASSERT_TRUE(session);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read INIT message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_INIT);

      mg_message_init *msg_init = message->init_v;
      EXPECT_EQ(
          std::string(msg_init->client_name->data, msg_init->client_name->size),
          "MemgraphBolt/0.1");
      {
        ASSERT_EQ(mg_map_size(msg_init->auth_token), 1);

        const mg_value *scheme_val = mg_map_at(msg_init->auth_token, "scheme");
        ASSERT_TRUE(scheme_val);
        ASSERT_EQ(mg_value_get_type(scheme_val), MG_VALUE_TYPE_STRING);
        const mg_string *scheme = mg_value_string(scheme_val);
        ASSERT_EQ(std::string(scheme->data, scheme->size), "none");
      }

      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send FAILURE message.
    {
      mg_map *metadata = mg_map_make_empty(2);
      mg_map_insert_unsafe(
          metadata, "code",
          mg_value_make_string("Memgraph.ClientError.Security.Authenticated"));
      mg_map_insert_unsafe(metadata, "message",
                           mg_value_make_string("Authentication failure"));
      ASSERT_EQ(mg_session_send_failure_message(session, metadata), 0);
      mg_map_destroy(metadata);
    }
    mg_session_destroy(session);
  });
  mg_session_params *params = mg_session_params_make();
  mg_session_params_set_host(params, "127.0.0.1");
  mg_session_params_set_port(params, port);
  mg_session *session;
  ASSERT_EQ(mg_connect_ca(params, &session, (mg_allocator *)&allocator),
            MG_ERROR_CLIENT_ERROR);
  ASSERT_THAT(std::string(mg_session_error(session)),
              HasSubstr("Authentication failure"));
  EXPECT_EQ(mg_session_status(session), MG_SESSION_BAD);
  mg_session_params_destroy(params);
  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(ConnectTest, Success) {
  RunServer([](int sockfd) {
    // Perform handshake.
    {
      char handshake[20];
      ASSERT_EQ(RecvData(sockfd, handshake, 20), 0);
      ASSERT_EQ(std::string(handshake, 4), "\x60\x60\xB0\x17"s);
      ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x00\x01"s);
      ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 12, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 16, 4), "\x00\x00\x00\x00"s);

      uint32_t version = htobe32(1);
      ASSERT_EQ(SendData(sockfd, (char *)&version, 4), 0);
    }

    mg_session *session = mg_session_init(&mg_system_allocator);
    ASSERT_TRUE(session);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read INIT message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_INIT);

      mg_message_init *msg_init = message->init_v;
      EXPECT_EQ(
          std::string(msg_init->client_name->data, msg_init->client_name->size),
          "MemgraphBolt/0.1");
      {
        ASSERT_EQ(mg_map_size(msg_init->auth_token), 3);

        const mg_value *scheme_val = mg_map_at(msg_init->auth_token, "scheme");
        ASSERT_TRUE(scheme_val);
        ASSERT_EQ(mg_value_get_type(scheme_val), MG_VALUE_TYPE_STRING);
        const mg_string *scheme = mg_value_string(scheme_val);
        ASSERT_EQ(std::string(scheme->data, scheme->size), "basic");

        const mg_value *principal_val =
            mg_map_at(msg_init->auth_token, "principal");
        ASSERT_TRUE(principal_val);
        ASSERT_EQ(mg_value_get_type(principal_val), MG_VALUE_TYPE_STRING);
        const mg_string *principal = mg_value_string(principal_val);
        ASSERT_EQ(std::string(principal->data, principal->size), "user");

        const mg_value *credentials_val =
            mg_map_at(msg_init->auth_token, "credentials");
        ASSERT_TRUE(credentials_val);
        ASSERT_EQ(mg_value_get_type(credentials_val), MG_VALUE_TYPE_STRING);
        const mg_string *credentials = mg_value_string(credentials_val);
        ASSERT_EQ(std::string(credentials->data, credentials->size), "pass");
      }

      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS message.
    ASSERT_EQ(mg_session_send_success_message(session, &mg_empty_map), 0);

    mg_session_destroy(session);
  });
  mg_session_params *params = mg_session_params_make();
  mg_session_params_set_host(params, "127.0.0.1");
  mg_session_params_set_port(params, port);
  mg_session_params_set_username(params, "user");
  mg_session_params_set_password(params, "pass");
  mg_session *session;
  ASSERT_EQ(mg_connect_ca(params, &session, (mg_allocator *)&allocator), 0);
  EXPECT_EQ(mg_session_status(session), MG_SESSION_READY);
  mg_session_params_destroy(params);
  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(ConnectTest, SuccessWithSSL) {
  RunServer([](int sockfd) {
    // Perform handshake.
    {
      char handshake[20];
      ASSERT_EQ(RecvData(sockfd, handshake, 20), 0);
      ASSERT_EQ(std::string(handshake, 4), "\x60\x60\xB0\x17"s);
      ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x00\x01"s);
      ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 12, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 16, 4), "\x00\x00\x00\x00"s);

      uint32_t version = htobe32(1);
      ASSERT_EQ(SendData(sockfd, (char *)&version, 4), 0);
    }

    mg_session *session = mg_session_init(&mg_system_allocator);
    ASSERT_TRUE(session);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read INIT message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_INIT);

      mg_message_init *msg_init = message->init_v;
      EXPECT_EQ(
          std::string(msg_init->client_name->data, msg_init->client_name->size),
          "MemgraphBolt/0.1");
      {
        ASSERT_EQ(mg_map_size(msg_init->auth_token), 3);

        const mg_value *scheme_val = mg_map_at(msg_init->auth_token, "scheme");
        ASSERT_TRUE(scheme_val);
        ASSERT_EQ(mg_value_get_type(scheme_val), MG_VALUE_TYPE_STRING);
        const mg_string *scheme = mg_value_string(scheme_val);
        ASSERT_EQ(std::string(scheme->data, scheme->size), "basic");

        const mg_value *principal_val =
            mg_map_at(msg_init->auth_token, "principal");
        ASSERT_TRUE(principal_val);
        ASSERT_EQ(mg_value_get_type(principal_val), MG_VALUE_TYPE_STRING);
        const mg_string *principal = mg_value_string(principal_val);
        ASSERT_EQ(std::string(principal->data, principal->size), "user");

        const mg_value *credentials_val =
            mg_map_at(msg_init->auth_token, "credentials");
        ASSERT_TRUE(credentials_val);
        ASSERT_EQ(mg_value_get_type(credentials_val), MG_VALUE_TYPE_STRING);
        const mg_string *credentials = mg_value_string(credentials_val);
        ASSERT_EQ(std::string(credentials->data, credentials->size), "pass");
      }

      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS message.
    ASSERT_EQ(mg_session_send_success_message(session, &mg_empty_map), 0);

    mg_session_destroy(session);
  });

  mg_secure_transport_init_called = 0;
  trust_callback_ok = 0;

  mg_session_params *params = mg_session_params_make();
  mg_session_params_set_host(params, "localhost");
  mg_session_params_set_port(params, port);
  mg_session_params_set_username(params, "user");
  mg_session_params_set_password(params, "pass");
  mg_session_params_set_sslmode(params, MG_SSLMODE_REQUIRE);
  mg_session_params_set_sslcert(params, "/path/to/cert");
  mg_session_params_set_sslkey(params, "/path/to/key");
  mg_session_params_set_trust_callback(params, trust_callback);
  int trust_data = 42;
  mg_session_params_set_trust_data(params, (void *)&trust_data);
  mg_session *session;
  ASSERT_EQ(mg_connect_ca(params, &session, (mg_allocator *)&allocator), 0);
  ASSERT_EQ(mg_secure_transport_init_called, 1);
  ASSERT_EQ(trust_callback_ok, 1);
  EXPECT_EQ(mg_session_status(session), MG_SESSION_READY);
  mg_session_params_destroy(params);
  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

class RunTest : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    int tmp[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, tmp);
    sc = tmp[0];
    ss = tmp[1];

    session = mg_session_init((mg_allocator *)&allocator);
    mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                          (mg_allocator *)&allocator);
    session->status = MG_SESSION_READY;
  }

  void RunServer(const std::function<void(int)> &server_func) {
    server_thread = std::thread(server_func, ss);
  }
  void StopServer() {
    if (server_thread.joinable()) {
      server_thread.join();
    }
  }

  int sc;
  int ss;
  mg_session *session;
  std::thread server_thread;

  tracking_allocator allocator;
};

bool CheckColumns(const mg_result *result,
                  const std::vector<std::string> &expColumns) {
  const mg_list *columns = mg_result_columns(result);
  if (!columns) {
    return false;
  }
  if (mg_list_size(columns) != expColumns.size()) {
    return false;
  }
  for (uint32_t i = 0; i < mg_list_size(columns); ++i) {
    const mg_value *name_val = mg_list_at(columns, i);
    if (mg_value_get_type(name_val) != MG_VALUE_TYPE_STRING) {
      return false;
    }
    const mg_string *name = mg_value_string(name_val);
    if (std::string(name->data, name->size) != expColumns[i]) {
      return false;
    }
  }
  return true;
}

bool CheckSummary(const mg_result *result, double exp_execution_time) {
  const mg_map *summary = mg_result_summary(result);
  if (!summary) {
    return false;
  }
  const mg_value *execution_time = mg_map_at(summary, "execution_time");
  if (!execution_time ||
      mg_value_get_type(execution_time) != MG_VALUE_TYPE_FLOAT) {
    return false;
  }
  return mg_value_float(execution_time) == exp_execution_time;
}

TEST_F(RunTest, ProtocolViolation) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read RUN message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
      mg_message_run *msg_run = message->run_v;

      EXPECT_EQ(std::string(msg_run->statement->data, msg_run->statement->size),
                "MATCH (n) RETURN n");

      ASSERT_EQ(mg_map_size(msg_run->parameters), 0);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send an unexpected RECORD message.
    {
      mg_list *fields = mg_list_make_empty(0);
      ASSERT_EQ(mg_session_send_record_message(session, fields), 0);
      mg_list_destroy(fields);
    }

    mg_session_destroy(session);
  });
  ASSERT_EQ(mg_session_run(session, "MATCH (n) RETURN n", NULL, NULL),
            MG_ERROR_PROTOCOL_VIOLATION);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_BAD);
  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, InvalidStatement) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read RUN message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
      mg_message_run *msg_run = message->run_v;
      EXPECT_EQ(std::string(msg_run->statement->data, msg_run->statement->size),
                "MATCH (n) RETURN m");
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send FAILURE because of a syntax error.
    {
      mg_map *summary = mg_map_make_empty(2);
      mg_map_insert_unsafe(
          summary, "code",
          mg_value_make_string("Memgraph.ClientError.Statement.SyntaxError"));
      mg_map_insert_unsafe(summary, "message",
                           mg_value_make_string("Unbound variable: m"));
      ASSERT_EQ(mg_session_send_failure_message(session, summary), 0);
      mg_map_destroy(summary);
    }

    // Client must send ACK_FAILURE now.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_ACK_FAILURE);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Server responds with SUCCESS.
    { ASSERT_EQ(mg_session_send_success_message(session, &mg_empty_map), 0); }

    mg_session_destroy(session);
  });
  ASSERT_EQ(mg_session_run(session, "MATCH (n) RETURN m", NULL, NULL),
            MG_ERROR_CLIENT_ERROR);
  ASSERT_THAT(std::string(mg_session_error(session)),
              HasSubstr("Unbound variable: m"));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);
  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, OkNoResults) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read RUN message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
      mg_message_run *msg_run = message->run_v;
      EXPECT_EQ(std::string(msg_run->statement->data, msg_run->statement->size),
                "MATCH (n) RETURN n");
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS to client.
    {
      mg_map *summary = mg_map_make_empty(2);
      mg_list *fields = mg_list_make_empty(1);
      mg_list_append(fields, mg_value_make_string("n"));
      mg_map_insert_unsafe(summary, "fields", mg_value_make_list(fields));
      mg_map_insert_unsafe(summary, "result_available_after",
                           mg_value_make_float(0.01));

      ASSERT_EQ(mg_session_send_success_message(session, summary), 0);
      mg_map_destroy(summary);
    }

    // Read PULL_ALL.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL_ALL);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS again because there are no results.
    {
      mg_map *metadata = mg_map_make_empty(1);
      mg_map_insert_unsafe(metadata, "execution_time",
                           mg_value_make_float(0.01));
      ASSERT_EQ(mg_session_send_success_message(session, metadata), 0);
      mg_map_destroy(metadata);
    }

    mg_session_destroy(session);
  });

  ASSERT_EQ(mg_session_run(session, "MATCH (n) RETURN n", NULL, NULL), 0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  mg_result *result;
  ASSERT_EQ(mg_session_pull(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_pull(session, &result), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, MultipleQueries) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    for (int i = 0; i < 10; ++i) {
      // Read RUN message.
      {
        mg_message *message;
        ASSERT_EQ(mg_session_receive_message(session), 0);
        ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
        ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
        mg_message_run *msg_run = message->run_v;
        EXPECT_EQ(
            std::string(msg_run->statement->data, msg_run->statement->size),
            "RETURN " + std::to_string(i) + " AS n");
        ASSERT_EQ(mg_map_size(msg_run->parameters), 0);
        mg_message_destroy_ca(message, session->decoder_allocator);
      }

      // Send SUCCESS to client.
      {
        mg_map *summary = mg_map_make_empty(2);
        mg_list *fields = mg_list_make_empty(1);
        mg_list_append(fields, mg_value_make_string("n"));
        mg_map_insert_unsafe(summary, "fields", mg_value_make_list(fields));
        mg_map_insert_unsafe(summary, "result_available_after",
                             mg_value_make_float(0.01));

        ASSERT_EQ(mg_session_send_success_message(session, summary), 0);
        mg_map_destroy(summary);
      }

      // Read PULL_ALL.
      {
        mg_message *message;
        ASSERT_EQ(mg_session_receive_message(session), 0);
        ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
        ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL_ALL);
        mg_message_destroy_ca(message, session->decoder_allocator);
      }

      // Send RECORD message to client.
      {
        mg_list *fields = mg_list_make_empty(1);
        mg_list_append(fields, mg_value_make_integer(i));
        ASSERT_EQ(mg_session_send_record_message(session, fields), 0);
        mg_list_destroy(fields);
      }

      // Send SUCCESS with execution summary.
      {
        mg_map *metadata = mg_map_make_empty(1);
        mg_map_insert_unsafe(metadata, "execution_time",
                             mg_value_make_float(0.01));
        ASSERT_EQ(mg_session_send_success_message(session, metadata), 0);
        mg_map_destroy(metadata);
      }
    }

    mg_session_destroy(session);
  });

  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(mg_session_run(session,
                             ("RETURN " + std::to_string(i) + " AS n").c_str(),
                             NULL, NULL),
              0);

    ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

    mg_result *result;

    // Check result.
    ASSERT_EQ(mg_session_pull(session, &result), 1);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n"}));

    const mg_list *row = mg_result_row(result);
    EXPECT_EQ(mg_list_size(row), 1);
    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 0)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 0)), i);

    ASSERT_EQ(mg_session_pull(session, &result), 0);
    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n"}));
    ASSERT_TRUE(CheckSummary(result, 0.01));
    ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

    ASSERT_EQ(mg_session_pull(session, &result), MG_ERROR_BAD_CALL);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);
  }

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, OkWithResults) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read RUN message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
      mg_message_run *msg_run = message->run_v;
      EXPECT_EQ(std::string(msg_run->statement->data, msg_run->statement->size),
                "UNWIND [1, 2, 3] AS n RETURN n, n + 5 AS m");
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS to client.
    {
      mg_map *summary = mg_map_make_empty(2);
      mg_list *fields = mg_list_make_empty(2);
      mg_list_append(fields, mg_value_make_string("n"));
      mg_list_append(fields, mg_value_make_string("m"));
      mg_map_insert_unsafe(summary, "fields", mg_value_make_list(fields));
      mg_map_insert_unsafe(summary, "result_available_after",
                           mg_value_make_float(0.01));

      ASSERT_EQ(mg_session_send_success_message(session, summary), 0);
      mg_map_destroy(summary);
    }

    // Read PULL_ALL.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL_ALL);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send 3 RECORD messages to client.
    {
      for (int i = 1; i <= 3; ++i) {
        mg_list *fields = mg_list_make_empty(2);
        mg_list_append(fields, mg_value_make_integer(i));
        mg_list_append(fields, mg_value_make_integer(i + 5));
        ASSERT_EQ(mg_session_send_record_message(session, fields), 0);
        mg_list_destroy(fields);
      }
    }

    // Send SUCCESS with execution summary.
    {
      mg_map *metadata = mg_map_make_empty(1);
      mg_map_insert_unsafe(metadata, "execution_time",
                           mg_value_make_float(0.01));
      ASSERT_EQ(mg_session_send_success_message(session, metadata), 0);
      mg_map_destroy(metadata);
    }

    mg_session_destroy(session);
  });

  ASSERT_EQ(
      mg_session_run(session, "UNWIND [1, 2, 3] AS n RETURN n, n + 5 AS m",
                     NULL, NULL),
      0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  mg_result *result;

  // Check results.
  for (int i = 1; i <= 3; ++i) {
    ASSERT_EQ(mg_session_pull(session, &result), 1);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));

    const mg_list *row = mg_result_row(result);
    EXPECT_EQ(mg_list_size(row), 2);
    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 0)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 0)), i);

    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 1)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 1)), i + 5);
  }

  ASSERT_EQ(mg_session_pull(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_pull(session, &result), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, QueryRuntimeError) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read RUN message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
      mg_message_run *msg_run = message->run_v;
      EXPECT_EQ(std::string(msg_run->statement->data, msg_run->statement->size),
                "MATCH (n) RETURN size(n.prop)");
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS to client.
    {
      mg_map *summary = mg_map_make_empty(2);
      mg_list *fields = mg_list_make_empty(1);
      mg_list_append(fields, mg_value_make_string("size(n.prop)"));
      mg_map_insert_unsafe(summary, "fields", mg_value_make_list(fields));
      mg_map_insert_unsafe(summary, "result_available_after",
                           mg_value_make_float(0.01));

      ASSERT_EQ(mg_session_send_success_message(session, summary), 0);
      mg_map_destroy(summary);
    }

    // Read PULL_ALL.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL_ALL);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // There was an error during execution, send FAILURE to client.
    {
      mg_map *summary = mg_map_make_empty(2);
      mg_map_insert_unsafe(
          summary, "code",
          mg_value_make_string(
              "Memgraph.ClientError.MemgraphError.MemgraphError"));
      mg_map_insert_unsafe(
          summary, "message",
          mg_value_make_string(
              "'size' argument must be a string, a collection or a path."));
      mg_session_send_failure_message(session, summary);
      mg_map_destroy(summary);
    }

    // Client should send ACK_FAILURE now.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_ACK_FAILURE);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Server responds with SUCCESS.
    { ASSERT_EQ(mg_session_send_success_message(session, &mg_empty_map), 0); }

    mg_session_destroy(session);
  });

  ASSERT_EQ(
      mg_session_run(session, "MATCH (n) RETURN size(n.prop)", NULL, NULL), 0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  mg_result *result;

  ASSERT_EQ(mg_session_pull(session, &result), MG_ERROR_CLIENT_ERROR);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_pull(session, &result), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, QueryDatabaseError) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read INIT message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
      mg_message_run *msg_run = message->run_v;
      EXPECT_EQ(std::string(msg_run->statement->data, msg_run->statement->size),
                "MATCH (n) RETURN size(n.prop)");
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS to client.
    {
      mg_map *summary = mg_map_make_empty(2);
      mg_list *fields = mg_list_make_empty(1);
      mg_list_append(fields, mg_value_make_string("size(n.prop)"));
      mg_map_insert_unsafe(summary, "fields", mg_value_make_list(fields));
      mg_map_insert_unsafe(summary, "result_available_after",
                           mg_value_make_float(0.01));

      ASSERT_EQ(mg_session_send_success_message(session, summary), 0);
      mg_map_destroy(summary);
    }
    // Read PULL_ALL.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL_ALL);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Crash and burn and send random bytes to client.
    {
      mg_session_write_uint8(session, 0x12);
      mg_session_write_uint8(session, 0x34);
      mg_session_flush_message(session);
    }

    mg_session_destroy(session);
  });

  ASSERT_EQ(
      mg_session_run(session, "MATCH (n) RETURN size(n.prop)", NULL, NULL), 0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  mg_result *result;

  ASSERT_NE(mg_session_pull(session, &result), 0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_BAD);

  ASSERT_EQ(mg_session_pull(session, &result), MG_ERROR_BAD_CALL);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, RunWithParams) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read RUN message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
      mg_message_run *msg_run = message->run_v;
      EXPECT_EQ(std::string(msg_run->statement->data, msg_run->statement->size),
                "WITH $param AS x RETURN x");
      {
        ASSERT_EQ(mg_map_size(msg_run->parameters), 1);
        const mg_value *param = mg_map_at(msg_run->parameters, "param");
        ASSERT_TRUE(param);
        ASSERT_EQ(mg_value_get_type(param), MG_VALUE_TYPE_INTEGER);
        ASSERT_EQ(mg_value_integer(param), 42);
      }
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS to client.
    {
      mg_map *summary = mg_map_make_empty(2);
      mg_list *fields = mg_list_make_empty(1);
      mg_list_append(fields, mg_value_make_string("x"));
      mg_map_insert_unsafe(summary, "fields", mg_value_make_list(fields));
      mg_map_insert_unsafe(summary, "result_available_after",
                           mg_value_make_float(0.01));

      ASSERT_EQ(mg_session_send_success_message(session, summary), 0);
      mg_map_destroy(summary);
    }

    // Read PULL_ALL.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL_ALL);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send RECORD messages to client.
    {
      mg_list *fields = mg_list_make_empty(1);
      mg_list_append(fields, mg_value_make_integer(42));
      ASSERT_EQ(mg_session_send_record_message(session, fields), 0);
      mg_list_destroy(fields);
    }

    // Send SUCCESS with execution summary.
    {
      mg_map *metadata = mg_map_make_empty(1);
      mg_map_insert_unsafe(metadata, "execution_time",
                           mg_value_make_float(0.01));
      ASSERT_EQ(mg_session_send_success_message(session, metadata), 0);
      mg_map_destroy(metadata);
    }

    mg_session_destroy(session);
  });

  mg_map *params = mg_map_make_empty(1);
  mg_map_insert_unsafe(params, "param", mg_value_make_integer(42));
  ASSERT_EQ(mg_session_run(session, "WITH $param AS x RETURN x", params, NULL),
            0);
  mg_map_destroy(params);

  mg_result *result;
  {
    ASSERT_EQ(mg_session_pull(session, &result), 1);
    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"x"}));
    const mg_list *row = mg_result_row(result);
    ASSERT_EQ(mg_list_size(row), 1);
    ASSERT_EQ(mg_value_get_type(mg_list_at(row, 0)), MG_VALUE_TYPE_INTEGER);
    ASSERT_EQ(mg_value_integer(mg_list_at(row, 0)), 42);
  }

  ASSERT_EQ(mg_session_pull(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"x"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}
