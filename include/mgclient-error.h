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

#ifndef MGCLIENT_ERROR_H
#define MGCLIENT_ERROR_H

// TODO(gitbuda): Refactor to mgclient-status or mgclient-code.

/// Success code.
#define MG_SUCCESS (0)

/// Failed to send data to server.
#define MG_ERROR_SEND_FAILED (-1)

/// Failed to receive data from server.
#define MG_ERROR_RECV_FAILED (-2)

/// Out of memory.
#define MG_ERROR_OOM (-3)

/// Trying to insert more values in a full container.
#define MG_ERROR_CONTAINER_FULL (-4)

/// Invalid value type was given as a function argument.
#define MG_ERROR_INVALID_VALUE (-5)

/// Failed to decode data returned from server.
#define MG_ERROR_DECODING_FAILED (-6)

/// Trying to insert a duplicate key in map.
#define MG_ERROR_DUPLICATE_KEY (-7)

/// An error occurred while trying to connect to server.
#define MG_ERROR_NETWORK_FAILURE (-8)

/// Invalid parameter supplied to \ref mg_connect.
#define MG_ERROR_BAD_PARAMETER (-9)

/// Server violated the Bolt protocol by sending an invalid message type or
/// invalid value.
#define MG_ERROR_PROTOCOL_VIOLATION (-10)

/// Server sent a FAILURE message containing ClientError code.
#define MG_ERROR_CLIENT_ERROR (-11)

/// Server sent a FAILURE message containing TransientError code.
#define MG_ERROR_TRANSIENT_ERROR (-12)

/// Server sent a FAILURE message containing DatabaseError code.
#define MG_ERROR_DATABASE_ERROR (-13)

/// Got an unknown error message from server.
#define MG_ERROR_UNKNOWN_ERROR (-14)

/// Invalid usage of the library.
#define MG_ERROR_BAD_CALL (-15)

/// Maximum container size allowed by Bolt exceeded.
#define MG_ERROR_SIZE_EXCEEDED (-16)

/// An error occurred during SSL connection negotiation.
#define MG_ERROR_SSL_ERROR (-17)

/// User provided trust callback returned a non-zeron value after SSL connection
/// negotiation.
#define MG_ERROR_TRUST_CALLBACK (-18)

// Unable to initialize the socket (both create and connect).
#define MG_ERROR_SOCKET (-100)

// Function unimplemented.
#define MG_ERROR_UNIMPLEMENTED (-1000)

#endif
