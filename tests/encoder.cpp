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

#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mgclient.h"
#include "mgcommon.h"
#include "mgconstants.h"
#include "mgsession.h"
#include "mgsocket.h"

#include "bolt-testdata.hpp"
#include "test-common.hpp"

using namespace std::string_literals;

/// Reads data from socket until it is closed. We read from socket while
/// writing to prevent the kernel buffer from filling up which makes `send`
/// block.
class TestServer {
 public:
  TestServer() {}

  void Start(int sockfd) {
    sockfd_ = sockfd;
    thread_ = std::thread([this, sockfd] {
      char buffer[8192];
      while (true) {
        ssize_t now = mg_socket_receive(sockfd, buffer, 8192);
        if (now < 0) {
          error = true;
          break;
        }
        if (now == 0) {
          break;
        }
        data.insert(data.end(), buffer, buffer + (size_t)now);
      }
    });
  }

  void Stop() {
    if (thread_.joinable()) {
      thread_.join();
    }
    close(sockfd_);
  }

  bool error{false};
  std::string data;

 private:
  int sockfd_;
  std::thread thread_;
};

class EncoderTest : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    session.out_buffer =
        (char *)malloc(MG_BOLT_CHUNK_HEADER_SIZE + MG_BOLT_MAX_CHUNK_SIZE);
    session.out_capacity = MG_BOLT_CHUNK_HEADER_SIZE + MG_BOLT_MAX_CHUNK_SIZE;
    session.out_begin = MG_BOLT_CHUNK_HEADER_SIZE;
    session.out_end = session.out_begin;
    {
      int tmp[2];
      ASSERT_EQ(mg_socket_pair(AF_UNIX, SOCK_STREAM, 0, tmp), 0);
      sc = tmp[0];
      ss = tmp[1];
    }
    mg_raw_transport_init(sc, (mg_raw_transport **)&session.transport,
                          (mg_allocator *)&allocator);
    server.Start(ss);
  }

  virtual void TearDown() override { free(session.out_buffer); }

  tracking_allocator allocator;
  mg_session session;
  TestServer server;

  int sc;
  int ss;
};

void AssertReadRaw(std::stringstream &sstr, const std::string &expected) {
  std::vector<char> tmp(expected.size());
  size_t read = sstr.readsome(tmp.data(), expected.size());
  if (read != expected.size()) {
    FAIL() << "Expected at least " << expected.size()
           << " bytes in stream, got only " << read;
  }
  std::string got(tmp.data(), expected.size());
  ASSERT_EQ(got, expected);
}

void AssertEnd(std::stringstream &sstr) {
  std::stringstream::char_type got = sstr.get();
  if (got != std::stringstream::traits_type::eof()) {
    FAIL() << "Expected end of input stream, got character "
           << ::testing::PrintToString(got);
  }
}

void AssertReadMessage(std::stringstream &sstr, const std::string &expected) {
  char chunk[65535];
  std::string got;
  while (true) {
    uint16_t chunk_size;
    if (sstr.readsome((char *)&chunk_size, 2) != 2) {
      FAIL() << "Not enough chunks in stream";
    }
    chunk_size = be16toh(chunk_size);
    if (chunk_size == 0) break;
    if (sstr.readsome(chunk, chunk_size) != chunk_size) {
      FAIL() << "Failed to read entire chunk from stream";
    }
    got.insert(got.end(), chunk, chunk + chunk_size);
  }
  ASSERT_EQ(got, expected);
}

class MessageChunkingTest : public EncoderTest {};

TEST_F(MessageChunkingTest, Empty) {
  mg_session_flush_message(&session);
  mg_raw_transport_destroy(session.transport);

  server.Stop();
  ASSERT_FALSE(server.error);
  std::stringstream sstr(server.data);

  ASSERT_READ_RAW(sstr, "\x00\x00"s);
  ASSERT_END(sstr);
  ASSERT_MEMORY_OK();
}

TEST_F(MessageChunkingTest, Small) {
  std::string data = "\x00\x01\x02\x03\x04\x05"s;
  mg_session_write_raw(&session, data.data(), data.size());
  mg_session_flush_message(&session);
  mg_raw_transport_destroy(session.transport);

  server.Stop();
  ASSERT_FALSE(server.error);
  std::stringstream sstr(server.data);

  ASSERT_READ_RAW(sstr, "\x00\x06"s);
  ASSERT_READ_RAW(sstr, data);
  ASSERT_READ_RAW(sstr, "\x00\x00"s);
  ASSERT_END(sstr);
  ASSERT_MEMORY_OK();
}

TEST_F(MessageChunkingTest, ExactlyOne) {
  std::string data(65535, '\0');
  for (int i = 0; i < 65535; ++i) {
    data[i] = (char)(i & 0xFF);
  }
  mg_session_write_raw(&session, data.data(), data.size());
  mg_session_flush_message(&session);
  mg_raw_transport_destroy(session.transport);

  server.Stop();
  ASSERT_FALSE(server.error);
  std::stringstream sstr(server.data);

  ASSERT_READ_RAW(sstr, "\xFF\xFF"s);
  ASSERT_READ_RAW(sstr, data);
  ASSERT_READ_RAW(sstr, "\x00\x00"s);
  ASSERT_END(sstr);
  ASSERT_MEMORY_OK();
}

TEST_F(MessageChunkingTest, ManySmall) {
  std::string data(1000, '\0');
  for (int i = 0; i < 1000; ++i) {
    data[i] = (char)(i & 0xff);
  }

  for (int i = 0; i < 100; ++i) {
    mg_session_write_raw(&session, data.data(), data.size());
  }
  mg_session_flush_message(&session);
  mg_raw_transport_destroy(session.transport);

  std::string first_chunk_data(65535, '\0');
  std::string second_chunk_data(100000 - 65535, '\0');
  {
    int p;
    for (p = 0; p < 65535; ++p) {
      first_chunk_data[p] = (char)((p % 1000) & 0xff);
    }
    for (; p < 100000; ++p) {
      second_chunk_data[p - 65535] = (char)((p % 1000) & 0xff);
    }
  }

  server.Stop();
  ASSERT_FALSE(server.error);
  std::stringstream sstr(server.data);

  ASSERT_READ_RAW(sstr, "\xFF\xFF"s);
  ASSERT_READ_RAW(sstr, first_chunk_data);
  ASSERT_READ_RAW(sstr, "\x86\xA1"s);
  ASSERT_READ_RAW(sstr, second_chunk_data);
  ASSERT_READ_RAW(sstr, "\x00\x00"s);
  ASSERT_END(sstr);
  ASSERT_MEMORY_OK();
}

TEST_F(MessageChunkingTest, ManyMessages) {
  mg_session_write_raw(&session, "abc", 3);
  mg_session_flush_message(&session);
  // An empty message in between.
  mg_session_flush_message(&session);
  mg_session_write_raw(&session, "defg", 4);
  mg_session_flush_message(&session);
  mg_raw_transport_destroy(session.transport);

  server.Stop();
  ASSERT_FALSE(server.error);
  std::stringstream sstr(server.data);

  ASSERT_READ_RAW(sstr, "\x00\x03"s);
  ASSERT_READ_RAW(sstr, "abc"s);
  ASSERT_READ_RAW(sstr, "\x00\x00"s);
  ASSERT_READ_RAW(sstr, "\x00\x00"s);
  ASSERT_READ_RAW(sstr, "\x00\x04"s);
  ASSERT_READ_RAW(sstr, "defg"s);
  ASSERT_READ_RAW(sstr, "\x00\x00"s);
  ASSERT_END(sstr);
  ASSERT_MEMORY_OK();
}

class ValueTest : public EncoderTest,
                  public ::testing::WithParamInterface<ValueTestParam> {
 protected:
  virtual void TearDown() { EncoderTest::TearDown(); }
};

// TODO(mtomic): When these tests fail, just a bunch of bytes is outputted, we
// might want to make this nicer (maybe add names or descriptions to
// parameters).

TEST_P(ValueTest, Encoding) {
  mg_session_write_value(&session, GetParam().decoded);
  mg_session_flush_message(&session);
  mg_raw_transport_destroy(session.transport);

  server.Stop();
  ASSERT_FALSE(server.error);
  std::stringstream sstr(server.data);

  ASSERT_READ_MESSAGE(sstr, GetParam().encoded);
  ASSERT_END(sstr);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(Null, ValueTest,
                        ::testing::ValuesIn(NullTestCases()), );

INSTANTIATE_TEST_CASE_P(Bool, ValueTest,
                        ::testing::ValuesIn(BoolTestCases()), );

INSTANTIATE_TEST_CASE_P(Integer, ValueTest,
                        ::testing::ValuesIn(IntegerTestCases()), );

INSTANTIATE_TEST_CASE_P(Float, ValueTest,
                        ::testing::ValuesIn(FloatTestCases()), );

INSTANTIATE_TEST_CASE_P(String, ValueTest,
                        ::testing::ValuesIn(StringTestCases()), );

INSTANTIATE_TEST_CASE_P(List, ValueTest,
                        ::testing::ValuesIn(ListTestCases()), );

INSTANTIATE_TEST_CASE_P(Date, ValueTest,
                        ::testing::ValuesIn(DateTestCases()), );

INSTANTIATE_TEST_CASE_P(LocalTime, ValueTest,
                        ::testing::ValuesIn(LocalTimeTestCases()), );

INSTANTIATE_TEST_CASE_P(LocalDateTime, ValueTest,
                        ::testing::ValuesIn(LocalDateTimeTestCases()), );

INSTANTIATE_TEST_CASE_P(Duration, ValueTest,
                        ::testing::ValuesIn((DurationTestCases())), );

INSTANTIATE_TEST_CASE_P(Map, ValueTest, ::testing::ValuesIn(MapTestCases()), );
