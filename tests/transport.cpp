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

// TODO(mtomic): Maybe add test for raw transport.

#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <thread>

#include <gtest/gtest.h>
#include <openssl/crypto.h>

#if MGCLIENT_ON_WINDOWS
// NOTE:
// https://stackoverflow.com/questions/49504648/x509-name-macro-in-c-wont-compile
#define WIN32_LEAN_AND_MEAN
#include <openssl/x509.h>
#endif  // MGCLIENT_ON_WINDOWS

extern "C" {
#include "mgclient.h"
#include "mgcommon.h"
#include "mgsocket.h"
#include "mgtransport.h"
}

#include "gmock_wrapper.h"
#include "test-common.hpp"

std::pair<X509 *, EVP_PKEY *> MakeCertAndKey(const char *name) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  RSA *rsa = RSA_new();
  BIGNUM *bne = BN_new();
  BN_set_word(bne, RSA_F4);
  RSA_generate_key_ex(rsa, 2048, bne, nullptr);
  BN_free(bne);

  EVP_PKEY *pkey = EVP_PKEY_new();
  EVP_PKEY_assign_RSA(pkey, rsa);
#else
  EVP_PKEY *pkey = EVP_RSA_gen(2048);
#endif

  X509 *x509 = X509_new();
  ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
  X509_gmtime_adj(X509_get_notBefore(x509), 0);
  X509_gmtime_adj(X509_get_notAfter(x509), 86400L);
  X509_set_pubkey(x509, pkey);
  X509_NAME *subject_name;
  subject_name = X509_get_subject_name(x509);
  X509_NAME_add_entry_by_txt(subject_name, "C", MBSTRING_ASC,
                             (unsigned char *)"CA", -1, -1, 0);
  X509_NAME_add_entry_by_txt(subject_name, "O", MBSTRING_ASC,
                             (unsigned char *)name, -1, -1, 0);
  X509_NAME_add_entry_by_txt(subject_name, "CN", MBSTRING_ASC,
                             (unsigned char *)"localhost", -1, -1, 0);
  return std::make_pair(x509, pkey);
}

extern int mg_init_ssl;

class SecureTransportTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    mg_init_ssl = 0;
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_crypto_strings();
#endif
  }
  virtual void SetUp() override {
    OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS, nullptr);
    int sv[2];
    ASSERT_EQ(mg_socket_pair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    sc = sv[0];
    ss = sv[1];

    // Build server key and certificate.
    std::tie(server_cert, server_key) = MakeCertAndKey("server");

    // Server certificate is self signed.
    X509_set_issuer_name(server_cert, X509_get_subject_name(server_cert));
    X509_sign(server_cert, server_key, EVP_sha512());

    // Build client key and certificate.
    X509 *client_cert;
    EVP_PKEY *client_key;
    std::tie(client_cert, client_key) = MakeCertAndKey("client");

    // Build CA key and certificate.
    EVP_PKEY *ca_key;
    std::tie(ca_cert, ca_key) = MakeCertAndKey("ca");

    // CA certificate is self signed.
    X509_set_issuer_name(ca_cert, X509_get_subject_name(ca_cert));
    X509_sign(ca_cert, ca_key, EVP_sha512());

    // Sign the client certificate with CA key.
    X509_set_issuer_name(client_cert, X509_get_subject_name(ca_cert));
    X509_sign(client_cert, ca_key, EVP_sha512());

    // Write client key and certificates to temporary file.
    client_cert_path = std::filesystem::temp_directory_path() / "client.crt";
    BIO *cert_file = BIO_new_file(client_cert_path.string().c_str(), "w");
    PEM_write_bio_X509(cert_file, client_cert);
    BIO_free(cert_file);

    client_key_path = std::filesystem::temp_directory_path() / "client.key";
    BIO *key_file = BIO_new_file(client_key_path.string().c_str(), "w");
    PEM_write_bio_PrivateKey(key_file, client_key, nullptr, nullptr, 0, nullptr,
                             nullptr);
    BIO_free(key_file);

    X509_free(client_cert);
    EVP_PKEY_free(client_key);
    EVP_PKEY_free(ca_key);
  }

  virtual void TearDown() override {
    X509_free(server_cert);
    X509_free(ca_cert);
    EVP_PKEY_free(server_key);
  }

  void RunServer(const std::function<void(void)> &server_function) {
    server_thread = std::thread(server_function);
  }

  void StopServer() {
    if (server_thread.joinable()) {
      server_thread.join();
    }
  }

  X509 *server_cert;
  X509 *ca_cert;
  EVP_PKEY *server_key;

  std::filesystem::path client_cert_path;
  std::filesystem::path client_key_path;

  int sc;
  int ss;
  std::thread server_thread;

  tracking_allocator allocator;
};

TEST_F(SecureTransportTest, NoCertificate) {
  // Initialize server.
  RunServer([this] {
    SSL_CTX *ctx;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    ctx = SSL_CTX_new(SSLv23_server_method());
#else
    ctx = SSL_CTX_new(TLS_server_method());
#endif
    SSL_CTX_use_certificate(ctx, server_cert);
    SSL_CTX_use_PrivateKey(ctx, server_key);
    ASSERT_TRUE(ctx);
    SSL *ssl = SSL_new(ctx);
    ASSERT_TRUE(ssl);
    SSL_set_fd(ssl, ss);
    ASSERT_EQ(SSL_accept(ssl), 1);

    char request[5];
    ASSERT_GT(SSL_read(ssl, request, 5), 0);
    ASSERT_EQ(strncmp(request, "hello", 5), 0);
    ASSERT_GT(SSL_write(ssl, "hello", 5), 0);

    SSL_free(ssl);
    SSL_CTX_free(ctx);
  });

  mg_transport *transport;
  ASSERT_EQ(mg_secure_transport_init(sc, nullptr, nullptr,
                                     (mg_secure_transport **)&transport,
                                     (mg_allocator *)&allocator),
            0);
  ASSERT_EQ(mg_transport_send((mg_transport *)transport, "hello", 5), 0);

  char response[5];
  ASSERT_EQ(mg_transport_recv(transport, response, 5), 0);
  ASSERT_EQ(strncmp(response, "hello", 5), 0);

  mg_transport_destroy(transport);

  StopServer();
}

TEST_F(SecureTransportTest, WithCertificate) {
  // Initialize server.
  RunServer([this] {
    SSL_CTX *ctx;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    ctx = SSL_CTX_new(SSLv23_server_method());
#else
    ctx = SSL_CTX_new(TLS_server_method());
#endif
    SSL_CTX_use_certificate(ctx, server_cert);
    SSL_CTX_use_PrivateKey(ctx, server_key);
    {
      X509_STORE *store = SSL_CTX_get_cert_store(ctx);
      X509_STORE_add_cert(store, ca_cert);
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       nullptr);

    ASSERT_TRUE(ctx);
    SSL *ssl = SSL_new(ctx);
    ASSERT_TRUE(ssl);
    SSL_set_fd(ssl, ss);
    if (SSL_accept(ssl) != 1) {
      ERR_print_errors_fp(stderr);
      FAIL();
    }

    char request[5];
    ASSERT_GT(SSL_read(ssl, request, 5), 0);
    ASSERT_EQ(strncmp(request, "hello", 5), 0);
    ASSERT_GT(SSL_write(ssl, "hello", 5), 0);

    SSL_free(ssl);
    SSL_CTX_free(ctx);
  });

  mg_transport *transport;
  ASSERT_EQ(mg_secure_transport_init(sc, client_cert_path.string().c_str(),
                                     client_key_path.string().c_str(),
                                     (mg_secure_transport **)&transport,
                                     (mg_allocator *)&allocator),
            0);
  ASSERT_EQ(mg_transport_send((mg_transport *)transport, "hello", 5), 0);

  char response[5];
  ASSERT_EQ(mg_transport_recv(transport, response, 5), 0);
  ASSERT_EQ(strncmp(response, "hello", 5), 0);

  mg_transport_destroy(transport);

  StopServer();
}
