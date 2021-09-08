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

#ifndef MGCLIENT_MGCONSTANTS_H
#define MGCLIENT_MGCONSTANTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define MG_BOLT_CHUNK_HEADER_SIZE 2
#define MG_BOLT_MAX_CHUNK_SIZE 65535

#define MG_TINY_INT_MIN -16
#define MG_TINY_INT_MAX 127

#define MG_TINY_SIZE_MAX 15

static const char MG_HANDSHAKE_MAGIC[] = "\x60\x60\xB0\x17";
static const char MG_USER_AGENT[] = "mgclient/" MGCLIENT_VERSION;

/// Markers
#define MG_MARKER_NULL 0xC0
#define MG_MARKER_BOOL_FALSE 0xC2
#define MG_MARKER_BOOL_TRUE 0xC3

#define MG_MARKER_INT_8 0xC8
#define MG_MARKER_INT_16 0xC9
#define MG_MARKER_INT_32 0xCA
#define MG_MARKER_INT_64 0xCB

#define MG_MARKER_FLOAT 0xC1

#define MG_MARKER_TINY_STRING 0x80
#define MG_MARKER_STRING_8 0xD0
#define MG_MARKER_STRING_16 0xD1
#define MG_MARKER_STRING_32 0xD2

#define MG_MARKER_TINY_LIST 0x90
#define MG_MARKER_LIST_8 0xD4
#define MG_MARKER_LIST_16 0xD5
#define MG_MARKER_LIST_32 0xD6

#define MG_MARKER_TINY_MAP 0xA0
#define MG_MARKER_MAP_8 0xD8
#define MG_MARKER_MAP_16 0xD9
#define MG_MARKER_MAP_32 0xDA

// These have to be ordered from smallest to largest because
// `mg_session_write_container_size` and `mg_session_read_container_size` depend
// on that.
static const uint8_t MG_MARKERS_STRING[] = {
    MG_MARKER_TINY_STRING, MG_MARKER_STRING_8, MG_MARKER_STRING_16,
    MG_MARKER_STRING_32};

static const uint8_t MG_MARKERS_LIST[] = {MG_MARKER_TINY_LIST, MG_MARKER_LIST_8,
                                          MG_MARKER_LIST_16, MG_MARKER_LIST_32};

static const uint8_t MG_MARKERS_MAP[] = {MG_MARKER_TINY_MAP, MG_MARKER_MAP_8,
                                         MG_MARKER_MAP_16, MG_MARKER_MAP_32};

#define MG_MARKER_TINY_STRUCT 0xB0
#define MG_MARKER_TINY_STRUCT1 0xB1
#define MG_MARKER_TINY_STRUCT2 0xB2
#define MG_MARKER_TINY_STRUCT3 0xB3
#define MG_MARKER_TINY_STRUCT4 0xB4
#define MG_MARKER_TINY_STRUCT5 0xB5
#define MG_MARKER_STRUCT_8 0xDC
#define MG_MARKER_STRUCT_16 0xDD

// Struct signatures
#define MG_SIGNATURE_NODE 0x4E
#define MG_SIGNATURE_RELATIONSHIP 0x52
#define MG_SIGNATURE_UNBOUND_RELATIONSHIP 0x72
#define MG_SIGNATURE_PATH 0x50
#define MG_SIGNATURE_DATE 0x44
#define MG_SIGNATURE_TIME 0x54
#define MG_SIGNATURE_LOCAL_TIME 0x74
#define MG_SIGNATURE_DATE_TIME 0x46
#define MG_SIGNATURE_DATE_TIME_ZONE_ID 0x66
#define MG_SIGNATURE_LOCAL_DATE_TIME 0x64
#define MG_SIGNATURE_DURATION 0x45
#define MG_SIGNATURE_POINT_2D 0x58
#define MG_SIGNATURE_POINT_3D 0x59
#define MG_SIGNATURE_MESSAGE_HELLO 0x01
#define MG_SIGNATURE_MESSAGE_RUN 0x10
#define MG_SIGNATURE_MESSAGE_PULL 0x3F
#define MG_SIGNATURE_MESSAGE_RECORD 0x71
#define MG_SIGNATURE_MESSAGE_SUCCESS 0x70
#define MG_SIGNATURE_MESSAGE_FAILURE 0x7F
#define MG_SIGNATURE_MESSAGE_ACK_FAILURE 0x0E
#define MG_SIGNATURE_MESSAGE_RESET 0x0F
#define MG_SIGNATURE_MESSAGE_BEGIN 0x11
#define MG_SIGNATURE_MESSAGE_COMMIT 0x12
#define MG_SIGNATURE_MESSAGE_ROLLBACK 0x13

#ifdef __cplusplus
}
#endif

#endif /* MGCLIENT_MGCONSTANTS_H */
