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

#include <gtest/gtest.h>

#include <optional>
#include <random>
#include <thread>

#include "mgclient.h"
#include "mgcommon.h"
#include "mgsession.h"
#include "mgsocket.h"

#include "gmock_wrapper.h"
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
  void (*suspend_until_ready_to_read)(struct mg_transport *);
  void (*suspend_until_ready_to_write)(struct mg_transport *);
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
  ttransport->suspend_until_ready_to_read = nullptr;
  ttransport->suspend_until_ready_to_write = nullptr;
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
    ssize_t sent_now =
        mg_socket_send(sockfd, buf + total_sent, len - total_sent);
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
    ssize_t received_now =
        mg_socket_receive(sockfd, buf + total_received, len - total_received);
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
      int sockfd = accept(ss, nullptr, nullptr);
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
    ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x01\x04"s);
    ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x01"s);
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
      ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x01\x04"s);
      ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x01"s);
      ASSERT_EQ(std::string(handshake + 12, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 16, 4), "\x00\x00\x00\x00"s);

      uint32_t version = htobe32(1);
      ASSERT_EQ(SendData(sockfd, (char *)&version, 4), 0);
    }

    mg_session *session = mg_session_init(&mg_system_allocator);
    ASSERT_TRUE(session);
    session->version = 1;
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
          MG_USER_AGENT);
      {
        ASSERT_EQ(mg_map_size(msg_init->auth_token), 1u);

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

TEST_F(ConnectTest, InitFail_v4) {
  RunServer([](int sockfd) {
    // Perform handshake.
    {
      char handshake[20];
      ASSERT_EQ(RecvData(sockfd, handshake, 20), 0);
      ASSERT_EQ(std::string(handshake, 4), "\x60\x60\xB0\x17"s);
      ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x01\x04"s);
      ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x01"s);
      ASSERT_EQ(std::string(handshake + 12, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 16, 4), "\x00\x00\x00\x00"s);

      uint32_t version = htobe32(0x0104);
      ASSERT_EQ(SendData(sockfd, (char *)&version, 4), 0);
    }

    mg_session *session = mg_session_init(&mg_system_allocator);
    ASSERT_TRUE(session);
    session->version = 4;
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read HELLO message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_HELLO);

      mg_message_hello *msg_hello = message->hello_v;
      {
        ASSERT_EQ(mg_map_size(msg_hello->extra), 2u);

        const mg_value *user_agent_val =
            mg_map_at(msg_hello->extra, "user_agent");
        ASSERT_TRUE(user_agent_val);
        ASSERT_EQ(mg_value_get_type(user_agent_val), MG_VALUE_TYPE_STRING);
        const mg_string *user_agent = mg_value_string(user_agent_val);
        ASSERT_EQ(std::string(user_agent->data, user_agent->size),
                  MG_USER_AGENT);

        const mg_value *scheme_val = mg_map_at(msg_hello->extra, "scheme");
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
      ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x01\x04"s);
      ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x01"s);
      ASSERT_EQ(std::string(handshake + 12, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 16, 4), "\x00\x00\x00\x00"s);

      uint32_t version = htobe32(1);
      ASSERT_EQ(SendData(sockfd, (char *)&version, 4), 0);
    }

    mg_session *session = mg_session_init(&mg_system_allocator);
    ASSERT_TRUE(session);
    session->version = 1;
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
          MG_USER_AGENT);
      {
        ASSERT_EQ(mg_map_size(msg_init->auth_token), 3u);

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

TEST_F(ConnectTest, Success_v4) {
  RunServer([](int sockfd) {
    // Perform handshake.
    {
      char handshake[20];
      ASSERT_EQ(RecvData(sockfd, handshake, 20), 0);
      ASSERT_EQ(std::string(handshake, 4), "\x60\x60\xB0\x17"s);
      ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x01\x04"s);
      ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x01"s);
      ASSERT_EQ(std::string(handshake + 12, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 16, 4), "\x00\x00\x00\x00"s);

      uint32_t version = htobe32(0x0104);
      ASSERT_EQ(SendData(sockfd, (char *)&version, 4), 0);
    }

    mg_session *session = mg_session_init(&mg_system_allocator);
    ASSERT_TRUE(session);
    session->version = 4;
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read HELLO message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_HELLO);

      mg_message_hello *msg_hello = message->hello_v;
      {
        ASSERT_EQ(mg_map_size(msg_hello->extra), 4u);

        const mg_value *user_agent_val =
            mg_map_at(msg_hello->extra, "user_agent");
        ASSERT_TRUE(user_agent_val);
        ASSERT_EQ(mg_value_get_type(user_agent_val), MG_VALUE_TYPE_STRING);
        const mg_string *user_agent = mg_value_string(user_agent_val);
        ASSERT_EQ(std::string(user_agent->data, user_agent->size),
                  MG_USER_AGENT);

        const mg_value *scheme_val = mg_map_at(msg_hello->extra, "scheme");
        ASSERT_TRUE(scheme_val);
        ASSERT_EQ(mg_value_get_type(scheme_val), MG_VALUE_TYPE_STRING);
        const mg_string *scheme = mg_value_string(scheme_val);
        ASSERT_EQ(std::string(scheme->data, scheme->size), "basic");

        const mg_value *principal_val =
            mg_map_at(msg_hello->extra, "principal");
        ASSERT_TRUE(principal_val);
        ASSERT_EQ(mg_value_get_type(principal_val), MG_VALUE_TYPE_STRING);
        const mg_string *principal = mg_value_string(principal_val);
        ASSERT_EQ(std::string(principal->data, principal->size), "user");

        const mg_value *credentials_val =
            mg_map_at(msg_hello->extra, "credentials");
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
      ASSERT_EQ(std::string(handshake + 4, 4), "\x00\x00\x01\x04"s);
      ASSERT_EQ(std::string(handshake + 8, 4), "\x00\x00\x00\x01"s);
      ASSERT_EQ(std::string(handshake + 12, 4), "\x00\x00\x00\x00"s);
      ASSERT_EQ(std::string(handshake + 16, 4), "\x00\x00\x00\x00"s);

      uint32_t version = htobe32(1);
      ASSERT_EQ(SendData(sockfd, (char *)&version, 4), 0);
    }

    mg_session *session = mg_session_init(&mg_system_allocator);
    ASSERT_TRUE(session);
    session->version = 1;
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
          MG_USER_AGENT);
      {
        ASSERT_EQ(mg_map_size(msg_init->auth_token), 3u);

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
    ASSERT_EQ(mg_socket_pair(AF_UNIX, SOCK_STREAM, 0, tmp), 0);
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

  void ProtocolViolation(int version);
  void InvalidStatement(int version);
  void OkNoResults(int version);
  void MultipleQueries(int version);
  void OkWithResults(int version);
  void QueryRuntimeError(int version);
  void QueryDatabaseError(int version);
  void RunWithParams(int version);
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

void RunTest::ProtocolViolation(int version) {
  RunServer([version](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = version;
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

      ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
      if (version == 4) {
        ASSERT_TRUE(msg_run->extra);
        ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
      }
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

  session->version = version;
  ASSERT_EQ(mg_session_run(session, "MATCH (n) RETURN n", nullptr, nullptr,
                           nullptr, nullptr),
            MG_ERROR_PROTOCOL_VIOLATION);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_BAD);
  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, ProtocolViolation_v1) { ProtocolViolation(1); }

TEST_F(RunTest, ProtocolViolation_v4) { ProtocolViolation(4); }

void RunTest::InvalidStatement(int version) {
  RunServer([version](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = version;
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
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
      if (version == 4) {
        ASSERT_TRUE(msg_run->extra);
        ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
      }
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

    if (version == 1) {
      // Client must send ACK_FAILURE now.
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_ACK_FAILURE);
      mg_message_destroy_ca(message, session->decoder_allocator);

    } else {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RESET);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Server responds with SUCCESS.
    { ASSERT_EQ(mg_session_send_success_message(session, &mg_empty_map), 0); }

    mg_session_destroy(session);
  });
  session->version = version;
  ASSERT_EQ(mg_session_run(session, "MATCH (n) RETURN m", nullptr, nullptr,
                           nullptr, nullptr),
            MG_ERROR_CLIENT_ERROR);
  ASSERT_THAT(std::string(mg_session_error(session)),
              HasSubstr("Unbound variable: m"));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);
  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, InvalidStatement_v1) { InvalidStatement(1); }

TEST_F(RunTest, InvalidStatement_v4) { InvalidStatement(4); }

void RunTest::OkNoResults(int version) {
  RunServer([version](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = version;
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
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
      if (version == 4) {
        ASSERT_TRUE(msg_run->extra);
        ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
      }
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
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
      if (version == 4) {
        mg_message_pull *pull_message = message->pull_v;
        ASSERT_TRUE(pull_message->extra);
        ASSERT_EQ(mg_map_size(pull_message->extra), 0u);
      }
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

  session->version = version;

  ASSERT_EQ(mg_session_run(session, "MATCH (n) RETURN n", nullptr, nullptr,
                           nullptr, nullptr),
            0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  mg_result *result;
  ASSERT_EQ(mg_session_pull(session, nullptr), 0);
  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, OkNoResults_v1) { OkNoResults(1); }

TEST_F(RunTest, OkNoResults_v4) { OkNoResults(4); }

void RunTest::MultipleQueries(int version) {
  RunServer([version](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = version;
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
        ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
        if (version == 4) {
          ASSERT_TRUE(msg_run->extra);
          ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
        }
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
        ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
        if (version == 4) {
          mg_message_pull *pull_message = message->pull_v;
          ASSERT_TRUE(pull_message->extra);
          ASSERT_EQ(mg_map_size(pull_message->extra), 0u);
        }
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

  session->version = version;

  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(mg_session_run(session,
                             ("RETURN " + std::to_string(i) + " AS n").c_str(),
                             nullptr, nullptr, nullptr, nullptr),
              0);

    ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

    mg_result *result;

    // Check result.
    ASSERT_EQ(mg_session_pull(session, nullptr), 0);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);
    ASSERT_EQ(mg_session_fetch(session, &result), 1);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);

    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n"}));

    const mg_list *row = mg_result_row(result);
    EXPECT_EQ(mg_list_size(row), 1u);
    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 0)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 0)), i);

    ASSERT_EQ(mg_session_fetch(session, &result), 0);
    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n"}));
    ASSERT_TRUE(CheckSummary(result, 0.01));
    ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

    ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);
  }

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, MultipleQueries_v1) { MultipleQueries(1); }

TEST_F(RunTest, MultipleQueries_v4) { MultipleQueries(4); }

void RunTest::OkWithResults(int version) {
  RunServer([version](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = version;
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
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
      if (version == 4) {
        ASSERT_TRUE(msg_run->extra);
        ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
      }
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
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
      if (version == 4) {
        mg_message_pull *pull_message = message->pull_v;
        ASSERT_TRUE(pull_message->extra);
        ASSERT_EQ(mg_map_size(pull_message->extra), 0u);
      }
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

  session->version = version;

  ASSERT_EQ(
      mg_session_run(session, "UNWIND [1, 2, 3] AS n RETURN n, n + 5 AS m",
                     nullptr, nullptr, nullptr, nullptr),
      0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  mg_result *result;

  ASSERT_EQ(mg_session_pull(session, nullptr), 0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);

  // Check results.
  for (int i = 1; i <= 3; ++i) {
    ASSERT_EQ(mg_session_fetch(session, &result), 1);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);

    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));

    const mg_list *row = mg_result_row(result);
    EXPECT_EQ(mg_list_size(row), 2u);
    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 0)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 0)), i);

    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 1)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 1)), i + 5);
  }

  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, OkWithResults_v1) { OkWithResults(1); }

TEST_F(RunTest, OkWithResults_v4) { OkWithResults(4); }

void RunTest::QueryRuntimeError(int version) {
  RunServer([version](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = version;
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
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
      if (version == 4) {
        ASSERT_TRUE(msg_run->extra);
        ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
      }
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
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
      if (version == 4) {
        mg_message_pull *pull_message = message->pull_v;
        ASSERT_TRUE(pull_message->extra);
        ASSERT_EQ(mg_map_size(pull_message->extra), 0u);
      }
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

    if (version == 1) {
      // Client should send ACK_FAILURE now.
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_ACK_FAILURE);
      mg_message_destroy_ca(message, session->decoder_allocator);

      // Server responds with SUCCESS.
    } else {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RESET);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    { ASSERT_EQ(mg_session_send_success_message(session, &mg_empty_map), 0); }
    mg_session_destroy(session);
  });

  session->version = version;

  ASSERT_EQ(mg_session_run(session, "MATCH (n) RETURN size(n.prop)", nullptr,
                           nullptr, nullptr, nullptr),
            0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  mg_result *result;

  ASSERT_EQ(mg_session_pull(session, nullptr), 0);
  ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_CLIENT_ERROR);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, QueryRuntimeError_v1) { QueryRuntimeError(1); }

TEST_F(RunTest, QueryRuntimeError_v4) { QueryRuntimeError(4); }

void RunTest::QueryDatabaseError(int version) {
  RunServer([version](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = version;
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
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
      if (version == 4) {
        ASSERT_TRUE(msg_run->extra);
        ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
      }
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
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
      if (version == 4) {
        mg_message_pull *pull_message = message->pull_v;
        ASSERT_TRUE(pull_message->extra);
        ASSERT_EQ(mg_map_size(pull_message->extra), 0u);
      }
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

  session->version = version;

  ASSERT_EQ(mg_session_run(session, "MATCH (n) RETURN size(n.prop)", nullptr,
                           nullptr, nullptr, nullptr),
            0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  mg_result *result;

  ASSERT_EQ(mg_session_pull(session, nullptr), 0);
  ASSERT_NE(mg_session_fetch(session, &result), 0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_BAD);

  ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, QueryDatabaseError_v1) { QueryDatabaseError(1); }

TEST_F(RunTest, QueryDatabaseError_v4) { QueryDatabaseError(4); }

void RunTest::RunWithParams(int version) {
  RunServer([version](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = version;
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
        ASSERT_EQ(mg_map_size(msg_run->parameters), 1u);
        const mg_value *param = mg_map_at(msg_run->parameters, "param");
        ASSERT_TRUE(param);
        ASSERT_EQ(mg_value_get_type(param), MG_VALUE_TYPE_INTEGER);
        ASSERT_EQ(mg_value_integer(param), 42);
        if (version == 4) {
          ASSERT_TRUE(msg_run->extra);
          ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
        }
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
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
      if (version == 4) {
        mg_message_pull *pull_message = message->pull_v;
        ASSERT_TRUE(pull_message->extra);
        ASSERT_EQ(mg_map_size(pull_message->extra), 0u);
      }
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

  session->version = version;

  mg_map *params = mg_map_make_empty(1);
  mg_map_insert_unsafe(params, "param", mg_value_make_integer(42));
  ASSERT_EQ(mg_session_run(session, "WITH $param AS x RETURN x", params,
                           nullptr, nullptr, nullptr),
            0);
  mg_map_destroy(params);

  mg_result *result;
  {
    ASSERT_EQ(mg_session_pull(session, nullptr), 0);
    ASSERT_EQ(mg_session_fetch(session, &result), 1);
    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"x"}));
    const mg_list *row = mg_result_row(result);
    ASSERT_EQ(mg_list_size(row), 1u);
    ASSERT_EQ(mg_value_get_type(mg_list_at(row, 0)), MG_VALUE_TYPE_INTEGER);
    ASSERT_EQ(mg_value_integer(mg_list_at(row, 0)), 42);
  }

  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"x"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, RunWithParams_v1) { RunWithParams(1); }

TEST_F(RunTest, RunWithParams_v4) { RunWithParams(4); }

/////////// Tests for Bolt v4 ///////////

mg_map *CreatePullInfo(int n = -1, std::optional<int> qid = std::nullopt) {
  int capacity = qid ? 2 : 1;
  mg_map *pull_info = mg_map_make_empty(capacity);
  if (!pull_info) {
    return nullptr;
  }

  mg_map_insert_unsafe(pull_info, "n", mg_value_make_integer(n));

  if (qid) {
    mg_map_insert_unsafe(pull_info, "qid", mg_value_make_integer(*qid));
  }

  return pull_info;
}

TEST_F(RunTest, MultipleResultPull) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = 4;
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
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
      ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
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

    // Read PULL 1 message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
      mg_message_pull *msg_pull = message->pull_v;
      ASSERT_EQ(mg_map_size(msg_pull->extra), 1u);
      const mg_value *n_val = mg_map_at(msg_pull->extra, "n");
      ASSERT_TRUE(n_val);
      ASSERT_EQ(n_val->type, MG_VALUE_TYPE_INTEGER);
      ASSERT_EQ(mg_value_integer(n_val), 1);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    const auto send_record = [&](int i) {
      mg_list *fields = mg_list_make_empty(2);
      mg_list_append(fields, mg_value_make_integer(i));
      mg_list_append(fields, mg_value_make_integer(i + 5));
      ASSERT_EQ(mg_session_send_record_message(session, fields), 0);
      mg_list_destroy(fields);
    };

    // Send 1 RECORD message to client
    send_record(1);

    // Send SUCCESS with has_more set to true.
    {
      mg_map *metadata = mg_map_make_empty(1);
      mg_map_insert_unsafe(metadata, "has_more", mg_value_make_bool(true));
      ASSERT_EQ(mg_session_send_success_message(session, metadata), 0);
      mg_map_destroy(metadata);
    }

    // Read PULL rest of messages.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
      mg_message_pull *msg_pull = message->pull_v;
      ASSERT_EQ(mg_map_size(msg_pull->extra), 1u);
      const mg_value *n_val = mg_map_at(msg_pull->extra, "n");
      ASSERT_TRUE(n_val);
      ASSERT_EQ(n_val->type, MG_VALUE_TYPE_INTEGER);
      ASSERT_EQ(mg_value_integer(n_val), -1);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send 2 RECORD messages to client.
    {
      for (int i = 2; i <= 3; ++i) {
        send_record(i);
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

  session->version = 4;

  ASSERT_EQ(
      mg_session_run(session, "UNWIND [1, 2, 3] AS n RETURN n, n + 5 AS m",
                     nullptr, nullptr, nullptr, nullptr),
      0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  {
    mg_map *pull_info = CreatePullInfo(1);
    ASSERT_EQ(mg_session_pull(session, pull_info), 0);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);
    mg_map_destroy(pull_info);
  }

  mg_result *result;
  const auto checkResults = [&](const int i) {
    ASSERT_EQ(mg_session_fetch(session, &result), 1);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);

    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));

    const mg_list *row = mg_result_row(result);
    EXPECT_EQ(mg_list_size(row), 2u);
    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 0)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 0)), i);

    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 1)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 1)), i + 5);
  };

  // Check first result
  checkResults(1);

  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));
  const mg_map *summary = mg_result_summary(result);
  ASSERT_TRUE(summary);
  const mg_value *has_more = mg_map_at(summary, "has_more");
  ASSERT_TRUE(has_more);
  ASSERT_EQ(mg_value_get_type(has_more), MG_VALUE_TYPE_BOOL);
  ASSERT_TRUE(mg_value_bool(has_more));

  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  // Pull rest of the results
  {
    mg_map *pull_info = CreatePullInfo(-1);
    ASSERT_EQ(mg_session_pull(session, pull_info), 0);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);
    mg_map_destroy(pull_info);
  }

  // Check results.
  for (int i = 2; i <= 3; ++i) {
    checkResults(i);
  }

  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_pull(session, nullptr), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, TransactionBasic) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = 4;
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    // Read BEGIN message
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_BEGIN);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS to client
    ASSERT_EQ(mg_session_send_success_message(session, &mg_empty_map), 0);

    // Read RUN message.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
      mg_message_run *msg_run = message->run_v;
      EXPECT_EQ(std::string(msg_run->statement->data, msg_run->statement->size),
                "MATCH (n) RETURN n");
      ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
      ASSERT_TRUE(msg_run->extra);
      ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS to client.
    {
      mg_map *summary = mg_map_make_empty(2);
      mg_list *fields = mg_list_make_empty(1);
      mg_list_append(fields, mg_value_make_string("n"));
      mg_map_insert_unsafe(summary, "fields", mg_value_make_list(fields));
      mg_map_insert_unsafe(summary, "qid", mg_value_make_integer(0));
      ASSERT_EQ(mg_session_send_success_message(session, summary), 0);
      mg_map_destroy(summary);
    }

    // Read PULL_ALL.
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
      mg_message_pull *pull_message = message->pull_v;
      ASSERT_TRUE(pull_message->extra);
      ASSERT_EQ(mg_map_size(pull_message->extra), 0u);
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

    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_ROLLBACK);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    {
      mg_map *metadata = mg_map_make_empty(1);
      mg_map_insert_unsafe(metadata, "execution_time",
                           mg_value_make_float(0.01));
      ASSERT_EQ(mg_session_send_success_message(session, metadata), 0);
      mg_map_destroy(metadata);
    }

    mg_session_destroy(session);
  });

  session->version = 4;

  ASSERT_EQ(mg_session_begin_transaction(session, nullptr), 0);

  ASSERT_EQ(mg_session_run(session, "MATCH (n) RETURN n", nullptr, nullptr,
                           nullptr, nullptr),
            0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  mg_result *result;
  ASSERT_EQ(mg_session_rollback_transaction(session, &result),
            MG_ERROR_BAD_CALL);

  ASSERT_EQ(mg_session_pull(session, nullptr), 0);
  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_rollback_transaction(session, &result), 0);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}

TEST_F(RunTest, TransactionWithMultipleRuns) {
  RunServer([](int sockfd) {
    mg_session *session = mg_session_init(&mg_system_allocator);
    session->version = 4;
    mg_raw_transport_init(sockfd, (mg_raw_transport **)&session->transport,
                          &mg_system_allocator);

    const auto read_run_message =
        [&](const std::string_view expected_statement) {
          mg_message *message;
          ASSERT_EQ(mg_session_receive_message(session), 0);
          ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
          ASSERT_EQ(message->type, MG_MESSAGE_TYPE_RUN);
          mg_message_run *msg_run = message->run_v;
          EXPECT_EQ(std::string_view(msg_run->statement->data,
                                     msg_run->statement->size),
                    expected_statement);
          ASSERT_EQ(mg_map_size(msg_run->parameters), 0u);
          ASSERT_EQ(mg_map_size(msg_run->extra), 0u);
          mg_message_destroy_ca(message, session->decoder_allocator);
        };

    const auto send_success_run = [&](const int qid) {
      mg_map *summary = mg_map_make_empty(2);
      mg_list *fields = mg_list_make_empty(2);
      mg_list_append(fields, mg_value_make_string("n"));
      mg_list_append(fields, mg_value_make_string("m"));
      mg_map_insert_unsafe(summary, "fields", mg_value_make_list(fields));
      mg_map_insert_unsafe(summary, "qid", mg_value_make_integer(qid));
      ASSERT_EQ(mg_session_send_success_message(session, summary), 0);
      mg_map_destroy(summary);
    };

    const auto read_pull_message = [&](const int expected_n,
                                       const std::optional<int> expected_qid =
                                           std::nullopt) {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_PULL);
      mg_message_pull *msg_pull = message->pull_v;

      const uint32_t extra_size = expected_qid ? 2u : 1u;
      ASSERT_EQ(mg_map_size(msg_pull->extra), extra_size);
      const mg_value *n_val = mg_map_at(msg_pull->extra, "n");
      ASSERT_TRUE(n_val);
      ASSERT_EQ(n_val->type, MG_VALUE_TYPE_INTEGER);
      ASSERT_EQ(mg_value_integer(n_val), expected_n);

      if (expected_qid) {
        const mg_value *qid_val = mg_map_at(msg_pull->extra, "qid");
        ASSERT_TRUE(qid_val);
        ASSERT_EQ(qid_val->type, MG_VALUE_TYPE_INTEGER);
        ASSERT_EQ(mg_value_integer(qid_val), *expected_qid);
      }
      mg_message_destroy_ca(message, session->decoder_allocator);
    };

    const auto send_record = [&](const int run_idx, const int result_idx) {
      mg_list *fields = mg_list_make_empty(2);
      const int n = 2 * run_idx + 1 + result_idx;
      mg_list_append(fields, mg_value_make_integer(n));
      mg_list_append(fields, mg_value_make_integer(n + 5));
      ASSERT_EQ(mg_session_send_record_message(session, fields), 0);
      mg_list_destroy(fields);
    };

    const auto send_has_more_success = [&]() {
      mg_map *metadata = mg_map_make_empty(1);
      mg_map_insert_unsafe(metadata, "has_more", mg_value_make_bool(true));
      ASSERT_EQ(mg_session_send_success_message(session, metadata), 0);
      mg_map_destroy(metadata);
    };

    const auto send_success_with_summary = [&]() {
      mg_map *metadata = mg_map_make_empty(1);
      mg_map_insert_unsafe(metadata, "execution_time",
                           mg_value_make_float(0.01));
      ASSERT_EQ(mg_session_send_success_message(session, metadata), 0);
      mg_map_destroy(metadata);
    };

    // Read BEGIN message
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_BEGIN);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    // Send SUCCESS to client
    ASSERT_EQ(mg_session_send_success_message(session, &mg_empty_map), 0);

    // Read first RUN message.
    read_run_message("UNWIND [1, 2] AS n RETURN n, n + 5 AS m");

    const int r1_qid = 0;
    // Send SUCCESS to client.
    send_success_run(r1_qid);

    // Read second RUN message
    read_run_message("UNWIND [3, 4] AS n RETURN n, n + 5 AS m");

    const int r2_qid = 1;
    // Send SUCCESS to client.
    send_success_run(r2_qid);

    // Read PULL 1 message from first run
    read_pull_message(1, r1_qid);

    // Send 1 RECORD message to client
    send_record(0, 0);

    // Send SUCCESS with has_more set to true.
    send_has_more_success();

    // Read third RUN
    read_run_message("UNWIND [5, 6] AS n RETURN n, n + 5 AS m");

    const int r3_qid = 2;
    // Send SUCCESS to client
    send_success_run(r3_qid);

    // Read PULL all messages from second run
    read_pull_message(-1, r2_qid);

    // Send records from second run
    send_record(1, 0);
    send_record(1, 1);

    // Send SUCCESS with execution summary for second run
    send_success_with_summary();

    // Read PULL 1 message from third run
    read_pull_message(1, r3_qid);

    // Send first record from third run
    send_record(2, 0);

    // Send SUCCESS with has more in third run
    send_has_more_success();

    // Read PULL all messages from first run
    read_pull_message(-1, r1_qid);

    // Send record from first run
    send_record(0, 1);

    // Send SUCCESS with execution summary for first run
    send_success_with_summary();

    // Read PULL all messages from third run
    read_pull_message(-1);

    // Send record from third run
    send_record(2, 1);

    // Send SUCCESS with execution summary for third run
    send_success_with_summary();

    // Read COMMIT transaction
    {
      mg_message *message;
      ASSERT_EQ(mg_session_receive_message(session), 0);
      ASSERT_EQ(mg_session_read_bolt_message(session, &message), 0);
      ASSERT_EQ(message->type, MG_MESSAGE_TYPE_COMMIT);
      mg_message_destroy_ca(message, session->decoder_allocator);
    }

    send_success_with_summary();

    mg_session_destroy(session);
  });

  session->version = 4;

  ASSERT_EQ(mg_session_begin_transaction(session, nullptr), 0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  int64_t r1_qid;
  ASSERT_EQ(mg_session_run(session, "UNWIND [1, 2] AS n RETURN n, n + 5 AS m",
                           nullptr, nullptr, nullptr, &r1_qid),
            0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  int64_t r2_qid;
  ASSERT_EQ(mg_session_run(session, "UNWIND [3, 4] AS n RETURN n, n + 5 AS m",
                           nullptr, nullptr, nullptr, &r2_qid),
            0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  // Pull first result from first run
  {
    mg_map *pull_info = CreatePullInfo(1, r1_qid);
    ASSERT_EQ(mg_session_pull(session, pull_info), 0);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);
    mg_map_destroy(pull_info);
  }

  mg_result *result;
  const auto check_result = [&](const int run_idx, const int result_idx) {
    ASSERT_EQ(mg_session_fetch(session, &result), 1);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);

    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));

    const int n = 2 * run_idx + 1 + result_idx;
    const mg_list *row = mg_result_row(result);
    EXPECT_EQ(mg_list_size(row), 2u);
    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 0)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 0)), n);

    EXPECT_EQ(mg_value_get_type(mg_list_at(row, 1)), MG_VALUE_TYPE_INTEGER);
    EXPECT_EQ(mg_value_integer(mg_list_at(row, 1)), n + 5);
  };

  // Check first result
  check_result(0, 0);

  // First run should have more results
  {
    ASSERT_EQ(mg_session_fetch(session, &result), 0);
    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));
    const mg_map *summary = mg_result_summary(result);
    ASSERT_TRUE(summary);
    const mg_value *has_more = mg_map_at(summary, "has_more");
    ASSERT_TRUE(has_more);
    ASSERT_EQ(mg_value_get_type(has_more), MG_VALUE_TYPE_BOOL);
    ASSERT_TRUE(mg_value_bool(has_more));

    ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);
  }

  // Send third RUN
  int64_t r3_qid;
  ASSERT_EQ(mg_session_run(session, "UNWIND [5, 6] AS n RETURN n, n + 5 AS m",
                           nullptr, nullptr, nullptr, &r3_qid),
            0);
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  // Pull all of the results from second run
  {
    mg_map *pull_info = CreatePullInfo(-1, r2_qid);
    ASSERT_EQ(mg_session_pull(session, pull_info), 0);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);
    mg_map_destroy(pull_info);
  }

  check_result(1, 0);
  check_result(1, 1);

  // Second run shouldn't have more results
  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  // Pull one result from third run
  {
    mg_map *pull_info = CreatePullInfo(1, r3_qid);
    ASSERT_EQ(mg_session_pull(session, pull_info), 0);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);
    mg_map_destroy(pull_info);
  }

  check_result(2, 0);

  // Third run should have more results
  {
    ASSERT_EQ(mg_session_fetch(session, &result), 0);
    ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));
    const mg_map *summary = mg_result_summary(result);
    ASSERT_TRUE(summary);
    const mg_value *has_more = mg_map_at(summary, "has_more");
    ASSERT_TRUE(has_more);
    ASSERT_EQ(mg_value_get_type(has_more), MG_VALUE_TYPE_BOOL);
    ASSERT_TRUE(mg_value_bool(has_more));
  }

  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  // Pull rest of the results from first run
  {
    mg_map *pull_info = CreatePullInfo(-1, r1_qid);
    ASSERT_EQ(mg_session_pull(session, pull_info), 0);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);
    mg_map_destroy(pull_info);
  }

  check_result(0, 1);

  // First run shouldn't have more results
  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_EXECUTING);

  // Pull rest of the results from third run
  {
    // If no qid is used, results from last run should be pulled
    mg_map *pull_info = CreatePullInfo(-1);
    ASSERT_EQ(mg_session_pull(session, pull_info), 0);
    ASSERT_EQ(mg_session_status(session), MG_SESSION_FETCHING);
    mg_map_destroy(pull_info);
  }

  check_result(2, 1);

  ASSERT_EQ(mg_session_fetch(session, &result), 0);
  ASSERT_TRUE(CheckColumns(result, std::vector<std::string>{"n", "m"}));
  ASSERT_TRUE(CheckSummary(result, 0.01));
  ASSERT_EQ(mg_session_status(session), MG_SESSION_READY);

  ASSERT_EQ(mg_session_fetch(session, &result), MG_ERROR_BAD_CALL);
  ASSERT_EQ(mg_session_pull(session, nullptr), MG_ERROR_BAD_CALL);

  ASSERT_EQ(mg_session_commit_transaction(session, &result), 0);

  mg_session_destroy(session);
  StopServer();
  ASSERT_MEMORY_OK();
}
