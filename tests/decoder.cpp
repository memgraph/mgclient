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

#include <iostream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "mgclient.h"
#include "mgcommon.h"
#include "mgsession.h"
#include "mgsocket.h"

#include "bolt-testdata.hpp"
#include "test-common.hpp"

using namespace std::string_literals;

/// Writes given data to socket. We must write from another thread and read in
/// parallel prevent the kernel buffer from filling up which makes `send` block.
class TestClient {
 public:
  TestClient() { mg_init(); }

  void Write(int sockfd, const std::string &data) {
    thread_ = std::thread([this, sockfd, data] {
      size_t sent = 0;
      while (sent < data.size()) {
        ssize_t now = mg_socket_send(sockfd, data.data(), data.size() - sent);
        if (now < 0) {
          auto socket_error = mg_socket_error();
          error = true;
          std::cout << "ERROR: " << socket_error << std::endl;
          break;
        }
        sent += now;
      }
    });
  }

  void WriteInChunks(int sockfd, const std::string &data) {
    std::string chunked_data;
    size_t num_chunks = (data.size() + 65534) / 65535;
    for (size_t i = 0; i < num_chunks; ++i) {
      size_t start = i * 65535;
      size_t end = std::min(data.size(), start + 65535);

      uint16_t chunk_size = htobe16(uint16_t(end - start));
      chunked_data.insert(chunked_data.end(), (char *)&chunk_size,
                          ((char *)&chunk_size) + 2);
      chunked_data.insert(chunked_data.end(), data.begin() + start,
                          data.begin() + end);
    }
    chunked_data += "\x00\x00"s;
    Write(sockfd, chunked_data);
  }

  void Stop() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  bool error{false};

 private:
  std::thread thread_;
};

class DecoderTest : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    int tmp[2];
    ASSERT_EQ(mg_socket_pair(AF_UNIX, SOCK_STREAM, 0, tmp), 0);
    sc = tmp[0];
    ss = tmp[1];
  }

  virtual void TearDown() override { client.Stop(); }

  tracking_allocator allocator;
  mg_session *session;
  TestClient client;

  int sc;
  int ss;
};

#define ASSERT_MEMORY_OK()                    \
  do {                                        \
    SCOPED_TRACE("ASSERT_MEMORY_OK");         \
    ASSERT_TRUE(allocator.allocated.empty()); \
  } while (0)

class MessageChunkingTest : public DecoderTest {};

TEST_F(MessageChunkingTest, Empty) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.Write(ss, "\x00\x00"s);
  int recv_message_status = mg_session_receive_message(session);
  if (recv_message_status != 0) {
    std::cout << "ERROR: " << mg_session_error(session) << std::endl;
  }
  EXPECT_EQ(recv_message_status, 0);
  std::string message(session->in_buffer, session->in_end);
  EXPECT_EQ(message, "");

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  EXPECT_NE(mg_session_receive_message(session), 0);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(MessageChunkingTest, Small) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  std::string data = "\x00\x01\x02\x03\x04\x05"s;

  client.Write(ss, "\x00\x06"s + data + "\x00\x00"s);

  EXPECT_EQ(mg_session_receive_message(session), 0);
  std::string message(session->in_buffer, session->in_end);
  EXPECT_EQ(message, data);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  EXPECT_NE(mg_session_receive_message(session), 0);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(MessageChunkingTest, ExactlyOne) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  std::string data(65535, '\0');
  for (int i = 0; i < 65535; ++i) {
    data[i] = (char)(i & 0xFF);
  }

  client.Write(ss, "\xFF\xFF"s + data + "\x00\x00"s);

  EXPECT_EQ(mg_session_receive_message(session), 0);
  std::string message(session->in_buffer, session->in_end);
  EXPECT_EQ(message, data);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  EXPECT_NE(mg_session_receive_message(session), 0);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(MessageChunkingTest, ManySmall) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  std::string part(1000, '\0');
  for (int i = 0; i < 1000; ++i) {
    part[i] = (char)(i & 0xff);
  }
  std::string data;
  data.reserve(100 * 1000);
  for (int i = 0; i < 100; ++i) data += part;

  client.Write(ss, "\xFF\xFF"s + data.substr(0, 65535) + "\x86\xA1"s +
                       data.substr(65535, 34465) + "\x00\x00"s);

  EXPECT_EQ(mg_session_receive_message(session), 0);
  std::string message(session->in_buffer, session->in_end);
  EXPECT_EQ(message, data);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  EXPECT_NE(mg_session_receive_message(session), 0);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

TEST_F(MessageChunkingTest, ManyMessages) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.Write(
      ss, "\x00\x03"s + "abc\x00\x00\x00\x00\x00\x04"s + "defg" + "\x00\x00"s);

  EXPECT_EQ(mg_session_receive_message(session), 0);
  std::string message1(session->in_buffer, session->in_end);
  EXPECT_EQ(message1, "abc");

  EXPECT_EQ(mg_session_receive_message(session), 0);
  std::string message2(session->in_buffer, session->in_end);
  EXPECT_EQ(message2, "");

  EXPECT_EQ(mg_session_receive_message(session), 0);
  std::string message3(session->in_buffer, session->in_end);
  EXPECT_EQ(message3, "defg");

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  EXPECT_NE(mg_session_receive_message(session), 0);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

class ValueTest : public DecoderTest,
                  public ::testing::WithParamInterface<ValueTestParam> {
 protected:
  virtual void TearDown() override { DecoderTest::TearDown(); }
};

// TODO(mtomic): When these tests fail, just a bunch of bytes is outputted, we
// might want to make this nicer (maybe add names or descriptions to
// parameters).

TEST_P(ValueTest, Decoding) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam().encoded);
  ASSERT_EQ(mg_session_receive_message(session), 0);

  mg_value *value;
  ASSERT_EQ(mg_session_read_value(session, &value), 0);
  EXPECT_TRUE(mg_value_equal(value, GetParam().decoded));
  mg_value_destroy_ca(value, session->decoder_allocator);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
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

INSTANTIATE_TEST_CASE_P(Map, ValueTest, ::testing::ValuesIn(MapTestCases()), );

INSTANTIATE_TEST_CASE_P(Node, ValueTest,
                        ::testing::ValuesIn(NodeTestCases()), );

INSTANTIATE_TEST_CASE_P(Relationship, ValueTest,
                        ::testing::ValuesIn(RelationshipTestCases()), );

INSTANTIATE_TEST_CASE_P(UnboundRelationship, ValueTest,
                        ::testing::ValuesIn(UnboundRelationshipTestCases()), );

INSTANTIATE_TEST_CASE_P(Path, ValueTest,
                        ::testing::ValuesIn(PathTestCases()), );

INSTANTIATE_TEST_CASE_P(Date, ValueTest,
                        ::testing::ValuesIn(DateTestCases()), );

INSTANTIATE_TEST_CASE_P(Time, ValueTest,
                        ::testing::ValuesIn(TimeTestCases()), );

INSTANTIATE_TEST_CASE_P(LocalTime, ValueTest,
                        ::testing::ValuesIn(LocalTimeTestCases()), );

INSTANTIATE_TEST_CASE_P(DateTime, ValueTest,
                        ::testing::ValuesIn(DateTimeTestCases()), );

INSTANTIATE_TEST_CASE_P(DateTimeZoneId, ValueTest,
                        ::testing::ValuesIn(DateTimeZoneIdTestCases()), );

INSTANTIATE_TEST_CASE_P(LocalDateTime, ValueTest,
                        ::testing::ValuesIn(LocalDateTimeTestCases()), );

INSTANTIATE_TEST_CASE_P(Duration, ValueTest,
                        ::testing::ValuesIn(DurationTestCases()), );

INSTANTIATE_TEST_CASE_P(Point2d, ValueTest,
                        ::testing::ValuesIn(Point2dTestCases()), );

INSTANTIATE_TEST_CASE_P(Point3d, ValueTest,
                        ::testing::ValuesIn(Point3dTestCases()), );

// TODO(mtomic): When these tests fail, just a bunch of bytes is outputted, we
// might want to make this nicer (maybe add names or descriptions to
// parameters). Also, it is hard to verify that decoding fails actually where we
// want it to fail (I did my best with GDB). It can be improved if we add custom
// error codes or a similar mechanism for specifying what went wrong.

class DecodingFailure : public DecoderTest,
                        public ::testing::WithParamInterface<std::string> {
 protected:
  virtual void TearDown() override { DecoderTest::TearDown(); }
};

class IntegerFailure : public DecodingFailure {};

TEST_P(IntegerFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  int64_t val;
  ASSERT_NE(mg_session_read_integer(session, &val), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(
    Test, IntegerFailure,
    ::testing::ValuesIn({""s, "\xC8"s, "\xC9\x01"s, "\xCA\x01\x02\x03"s,
                         "\xCB\x01\x02\x03\x04\x05\x06\x07"s, "\xCC"s}), );

class BoolFailure : public DecodingFailure {};

TEST_P(BoolFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  int val;
  ASSERT_NE(mg_session_read_bool(session, &val), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(Test, BoolFailure,
                        ::testing::ValuesIn({""s, "\xCC"s}), );

class FloatFailure : public DecodingFailure {};

TEST_P(FloatFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  double val;
  ASSERT_NE(mg_session_read_float(session, &val), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(
    Test, FloatFailure,
    ::testing::ValuesIn({""s, "\xCC"s, "\xC1\x01\x02\x03\x04\x05\x06\x07"s}), );

class StringFailure : public DecodingFailure {};

TEST_P(StringFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  mg_string *str;
  ASSERT_NE(mg_session_read_string(session, &str), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(Test, StringFailure,
                        ::testing::ValuesIn({""s, "\xCC"s, "\xD0"s, "\xD1\x01"s,
                                             "\xD2\x01\x02\x03"s,
                                             "\x85pqrs"s}), );

class ListFailure : public DecodingFailure {};

TEST_P(ListFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  mg_list *list;
  ASSERT_NE(mg_session_read_list(session, &list), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(Test, ListFailure,
                        ::testing::ValuesIn({""s, "\xCC"s, "\xD4"s, "\xD5\x01"s,
                                             "\xD6\x01\x02\x03"s,
                                             "\x93\x01\x02"s,
                                             "\x93\x01\x02\xCC"s}), );

class MapFailure : public DecodingFailure {};

TEST_P(MapFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  mg_map *map;
  ASSERT_NE(mg_session_read_map(session, &map), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(
    Test, MapFailure,
    ::testing::ValuesIn({""s, "\xCC"s, "\xD8"s, "\xD9\x01"s,
                         "\xDA\x01\x02\x03"s,
                         "\xA3\x81x\x01\x81y\xCC\x81z\x03"s,
                         "\xA3\x81x\x01\x81y\x02\x85z"s}), );

class NodeFailure : public DecodingFailure {};

TEST_P(NodeFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  mg_node *node;
  ASSERT_NE(mg_session_read_node(session, &node), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(
    Test, NodeFailure,
    ::testing::ValuesIn({""s, "\xB2\x4E"s, "\xB3\x5E"s, "\xB3\x4E"s,
                         "\xB3\x4E\xCC"s, "\xB3\x4E\x01\x95\x82L1\xCC"s,
                         "\xB3\x4E\x01\x92\x82L1\x82L2\xA2\x81x"s}), );

class RelationshipFailure : public DecodingFailure {};

TEST_P(RelationshipFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  mg_relationship *rel;
  ASSERT_NE(mg_session_read_relationship(session, &rel), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(
    Test, RelationshipFailure,
    ::testing::ValuesIn({""s, "\xB2\x52"s, "\xB5\x02"s, "\xB5\x52"s,
                         "\xB5\x52\xCC"s, "\xB5\x52\x01\xCC"s,
                         "\xB5\x52\x01\x02\xCC"s, "\xB5\x52\x01\x02\x03\xCC"s,
                         "\xB5\x52\x01\x02\x03\x84type\xCC"s}), );

class UnboundRelationshipFailure : public DecodingFailure {};

TEST_P(UnboundRelationshipFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  mg_unbound_relationship *rel;
  ASSERT_NE(mg_session_read_unbound_relationship(session, &rel), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(Test, UnboundRelationshipFailure,
                        ::testing::ValuesIn({""s, "\xB2\x72"s, "\xB3\x02"s,
                                             "\xB3\x72"s, "\xB3\x72\xCC"s,
                                             "\xB3\x72\x01\xCC"s,
                                             "\xB3\x72\x01\x84type\xCC"s}), );

class PathFailure : public DecodingFailure {};

TEST_P(PathFailure, Test) {
  session = mg_session_init((mg_allocator *)&allocator);
  mg_raw_transport_init(sc, (mg_raw_transport **)&session->transport,
                        (mg_allocator *)&allocator);
  ASSERT_TRUE(session);

  client.WriteInChunks(ss, GetParam());
  ASSERT_EQ(mg_session_receive_message(session), 0);

  mg_path *path;
  ASSERT_NE(mg_session_read_path(session, &path), 0);

  client.Stop();
  close(ss);
  ASSERT_FALSE(client.error);

  mg_session_destroy(session);
  ASSERT_MEMORY_OK();
}

INSTANTIATE_TEST_CASE_P(
    Test, PathFailure,
    ::testing::ValuesIn(
        {""s, "\xB2\x50"s, "\xB3\x02"s, "\xB3\x50"s, "\xB3\x50\x92"s,
         "\xB3\x50\x92\xB3\x4E\x01\x90\xA0\xB3\x4E\x02\x90\xA0"s,
         "\xB3\x50\x92\xB3\x4E\x01\x90\xA0\xB3\x4E\x02\x90\xA0\x92"s,
         "\xB3\x50\x92\xB3\x4E\x01\x90\xA0\xB3\x4E\x02\x90\xA0\x92\xB3\x72\x01\x84type\xA0\xB3\x72\x02\x84type\xA0"s,
         "\xB3\x50\x92\xB3\x4E\x01\x90\xA0\xB3\x4E\x02\x90\xA0\x92\xB3\x72\x01\x84type\xA0\xB3\x72\x02\x84type\xA0\x94"s,
         "\xB3\x50\x92\xB3\x4E\x01\x90\xA0\xB3\x4E\x02\x90\xA0\x92\xB3\x72\x01\x84type\xA0\xB3\x72\x02\x84type\xA0\x93\x01\x01\x01"s,
         "\xB3\x50\x92\xB3\x4E\x01\x90\xA0\xB3\x4E\x02\x90\xA0\x92\xB3\x72\x01\x84type\xA0\xB3\x72\x02\x84type\xA0\x94\xF0\x00\x01\x00"s,
         "\xB3\x50\x92\xB3\x4E\x01\x90\xA0\xB3\x4E\x02\x90\xA0\x92\xB3\x72\x01\x84type\xA0\xB3\x72\x02\x84type\xA0\x94\x01\x08\x01\x00"s}), );
