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

#ifndef MGCLIENT_MGCLIENT_H
#define MGCLIENT_MGCLIENT_H

#include "mgclient-export.h"

#ifdef __cplusplus
extern "C" {
#endif

/// \file mgclient.h
///
/// Provides \ref mg_session, a data type representing a connection to Bolt
/// server, along with functions for connecting to Bolt database and
/// executing queries against it, and \ref mg_value, a data type representing a
/// value in Bolt protocol along with supporting types and manipulation
/// functions for them.
///
/// \ref mg_session is an opaque data type representing a connection to Bolt
/// server. Commands can be submitted for execution using \ref mg_session_run
/// and results can be obtained using \ref mg_session_pull. A \ref mg_session
/// can execute at most one command at a time, and all results should be
/// consumed before trying to execute the next query.
///
/// The usual flow for execution of a single query would be the following:
///
///  1. Submit the command for execution using \ref mg_session_run.
///
///  2. Call \ref mg_session_pull until it returns 0 to consume result rows and
///     access result values using \ref mg_result_row.
///
///  3. If necessary, access command execution summary using \ref
///     mg_result_summary.
///
/// If any of the functions returns an error exit code, more detailed error
/// message can be obtained by calling \ref mg_session_error.
///
/// \ref mg_value is an opaque data type representing an arbitrary value of any
/// of the types specified by the Bolt protocol. It can encapsulate any of its
/// supporting types: \ref mg_string, \ref mg_list, \ref mg_map, \ref mg_node,
/// \ref mg_relationship, \ref mg_unbound_relationship and \ref mg_path.
/// Provided along with them are basic manipulation functions for those data
/// types. The API for most of data types is quite rudimentary, and as such is
/// not usable for complex operations on strings, maps, lists, etc. It is only
/// supposed to be used to construct data to be sent to the Bolt server, and
/// read data obtained from the Bolt server.
///
/// Each object has a corresponding \c mg_*_destroy function that should be
/// invoked on the object at the end of its lifetime to free the resources
/// allocated for its storage. Each object has an owner, that is responsible for
/// its destruction. Object can be owned by the API client or by another
/// object. When being destroyed, an object will also destroy all other
/// objects it owns. Therefore, API client is only responsible for
/// destroying the object it directly owns. For example, if the API client
/// constructed a \ref mg_list value and inserted some other \ref mg_value
/// objects into it, they must only invoke \ref mg_list_destroy on the list and
/// all of its members will be properly destroyed, because the list owns all of
/// its elements. Invoking \c mg_*_destroy on objects that are not owned by the
/// caller will usually result in memory corruption, double freeing, nuclear
/// apocalypse and similar unwanted behaviors. Therefore, object ownership
/// should be tracked carefully.
///
/// Invoking certain functions on objects might cause ownership changes.
/// Obviously, you shouldn't pass objects you don't own to functions that steal
/// ownership.
///
/// Function signatures are of big help in ownership tracking. Now follow two
/// simple rules, all functions that do not conform to those rules (if any) will
/// explicitly specify that in their documentation.
///
///  1. Return values
///
///     Functions that return a non-const pointer to an object give
///     ownership of the returned object to the caller. Examples are:
///       - creation functions (e.g. \ref mg_list_make_empty).
///       - copy functions (e.g. \ref mg_value_copy).
///       - \ref mg_connect has a `mg_session **` output parameter because the
///         API client becomes the owner of the \ref mg_session object
///
///     Functions that return a const pointer to a object provide
///     read-only access to the returned object that is valid only while the
///     owning object is alive. Examples are:
///       - access functions on `mg_value` (e.g. \ref mg_value_list).
///       - member access functions on containers (e.g. \ref mg_map_key_at,
///         \ref mg_list_at, \ref mg_map_at).
///       - field access functions on graph types (e.g. \ref
///         mg_node_properties).
///       - \ref mg_session_pull has a `const mg_result **` output parameter,
///         because the \ref mg_session object keeps ownership of the returned
///         result and destroys it on next pull
///
///  2. Function arguments
///
///     Functions that take a non-const pointer to a object either modify
///     it or change its ownership (it is usually obvious what happens).
///     Examples are:
///       - member insert functions on containers transfer the ownership of
///         inserted values to the container. They also take a non-const pointer
///         to the container because they modify it. Ownership of the container
///         is not changed (e.g. \ref mg_map_insert takes ownership of the
///         passed key and value).
///      - \ref mg_session_run takes a non-const pointer to the session because
///        it modifies it internal state, but there is no ownership change
///
///     An obvious exception here are \c mg_*_destroy functions which do not
///     change ownership of the object.
///
///     Functions that take a const pointer to a object do not change the
///     owner of the passed object nor they modify it. Examples are:
///       - member access functions on containers take const pointer to the
///         container (e.g. \ref mg_list_at, \ref mg_map_at, ...).
///       - member access functions on graph types take const pointer to the
///         container (e.g. \ref mg_path_node_at, \ref mg_node_label_count,
///         ...).
///       - copy functions.

#include <stdint.h>

/// Client software version.
///
/// \return Client version in the major.minor.patch format.
MGCLIENT_EXPORT const char *mg_client_version();

/// Initializes the client (the whole process).
/// Should be called at the beginning of each process using the client.
///
/// \return Zero if initialization was successful.
MGCLIENT_EXPORT int mg_init();

/// Finalizes the client (the whole process).
/// Should be called at the end of each process using the client.
MGCLIENT_EXPORT void mg_finalize();

/// An enum listing all the types as specified by Bolt protocol.
enum mg_value_type {
  MG_VALUE_TYPE_NULL,
  MG_VALUE_TYPE_BOOL,
  MG_VALUE_TYPE_INTEGER,
  MG_VALUE_TYPE_FLOAT,
  MG_VALUE_TYPE_STRING,
  MG_VALUE_TYPE_LIST,
  MG_VALUE_TYPE_MAP,
  MG_VALUE_TYPE_NODE,
  MG_VALUE_TYPE_RELATIONSHIP,
  MG_VALUE_TYPE_UNBOUND_RELATIONSHIP,
  MG_VALUE_TYPE_PATH,
  MG_VALUE_TYPE_DATE,
  MG_VALUE_TYPE_TIME,
  MG_VALUE_TYPE_LOCAL_TIME,
  MG_VALUE_TYPE_DATE_TIME,
  MG_VALUE_TYPE_DATE_TIME_ZONE_ID,
  MG_VALUE_TYPE_LOCAL_DATE_TIME,
  MG_VALUE_TYPE_DURATION,
  MG_VALUE_TYPE_POINT_2D,
  MG_VALUE_TYPE_POINT_3D,
  MG_VALUE_TYPE_UNKNOWN
};

/// A Bolt value, encapsulating all other values.
typedef struct mg_value mg_value;

/// An UTF-8 encoded string.
///
/// Note that the length of the string is the byte count of the UTF-8 encoded
/// data. It is guaranteed that the bytes of the string are stored contiguously,
/// and they can be accessed through a pointer to first element returned by
/// \ref mg_string_data.
///
/// Note that the library doesn't perform any checks whatsoever to see if the
/// provided data is a valid UTF-8 encoded string when constructing instances of
/// \ref mg_string.
///
/// Maximum possible string length allowed by Bolt protocol is \c UINT32_MAX.
typedef struct mg_string mg_string;

/// An ordered sequence of values.
///
/// List may contain a mixture of different types as its elements. A list owns
/// all values stored in it.
///
/// Maximum possible list length allowed by Bolt is \c UINT32_MAX.
typedef struct mg_list mg_list;

/// Sized sequence of pairs of keys and values.
///
/// Map may contain a mixture of different types as values. A map owns all keys
/// and values stored in it.
///
/// Maximum possible map size allowed by Bolt protocol is \c UINT32_MAX.
typedef struct mg_map mg_map;

/// Represents a node from a labeled property graph.
///
/// Consists of a unique identifier (withing the scope of its origin graph), a
/// list of labels and a map of properties. A node owns its labels and
/// properties.
///
/// Maximum possible number of labels allowed by Bolt protocol is \c UINT32_MAX.
typedef struct mg_node mg_node;

/// Represents a relationship from a labeled property graph.
///
/// Consists of a unique identifier (within the scope of its origin graph),
/// identifiers for the start and end nodes of that relationship, a type and a
/// map of properties. A relationship owns its type string and property map.
typedef struct mg_relationship mg_relationship;

/// Represents a relationship from a labeled property graph.
///
/// Like \ref mg_relationship, but without identifiers for start and end nodes.
/// Mainly used as a supporting type for \ref mg_path. An unbound relationship
/// owns its type string and property map.
typedef struct mg_unbound_relationship mg_unbound_relationship;

/// Represents a sequence of alternating nodes and relationships
/// corresponding to a walk in a labeled property graph.
///
/// A path of length L consists of L + 1 nodes indexed from 0 to L, and L
/// unbound relationships, indexed from 0 to L - 1. Each relationship has a
/// direction. A relationship is said to be reversed if it was traversed in the
/// direction opposite of the direction of the underlying relationship in the
/// data graph.
typedef struct mg_path mg_path;

/// \brief Represents a date.
///
/// Date is defined with number of days since the Unix epoch.
typedef struct mg_date mg_date;

/// \brief Represents time with its time zone.
///
/// Time is defined with nanoseconds since midnight.
/// Timezone is defined with seconds from UTC.
typedef struct mg_time mg_time;

/// \brief Represents local time.
///
/// Time is defined with nanoseconds since midnight.
typedef struct mg_local_time mg_local_time;

/// \brief Represents date and time with its time zone.
///
/// Date is defined with seconds since the adjusted Unix epoch.
/// Time is defined with nanoseconds since midnight.
/// Time zone is defined with minutes from UTC.
typedef struct mg_date_time mg_date_time;

/// \brief Represents date and time with its time zone.
///
/// Date is defined with seconds since the adjusted Unix epoch.
/// Time is defined with nanoseconds since midnight.
/// Timezone is defined with an identifier for a specific time zone.
typedef struct mg_date_time_zone_id mg_date_time_zone_id;

/// \brief Represents date and time without its time zone.
///
/// Date is defined with seconds since the Unix epoch.
/// Time is defined with nanoseconds since midnight.
typedef struct mg_local_date_time mg_local_date_time;

/// \brief Represents a temporal amount which captures the difference in time
/// between two instants.
///
/// Duration is defined with months, days, seconds, and nanoseconds.
/// \note
/// Duration can be negative.
typedef struct mg_duration mg_duration;

/// \brief Represents a single location in 2-dimensional space.
///
/// Contains SRID along with its x and y coordinates.
typedef struct mg_point_2d mg_point_2d;

/// \brief Represents a single location in 3-dimensional space.
///
/// Contains SRID along with its x, y and z coordinates.
typedef struct mg_point_3d mg_point_3d;

/// Constructs a nil \ref mg_value.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_null();

/// Constructs a boolean \ref mg_value.
///
/// \param val If the parameter is zero, constructed value will be false.
///            Otherwise, it will be true.
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_bool(int val);

/// Constructs an integer \ref mg_value with the given underlying value.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_integer(int64_t val);

/// Constructs a float \ref mg_value with the given underlying value.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_float(double val);

/// Constructs a string \ref mg_value given a null-terminated string.
///
/// A new \ref mg_string instance will be created from the null-terminated
/// string as the underlying value.
///
/// \param str A null-terminated UTF-8 string.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_string(const char *str);

/// Construct a string \ref mg_value given the underlying \ref mg_string.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_string2(mg_string *str);

/// Constructs a list \ref mg_value given the underlying \ref mg_list.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_list(mg_list *list);

/// Constructs a map \ref mg_value given the underlying \ref mg_map.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_map(mg_map *map);

/// Constructs a node \ref mg_value given the underlying \ref mg_node.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_node(mg_node *node);

/// Constructs a relationship \ref mg_value given the underlying
/// \ref mg_relationship.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_relationship(mg_relationship *rel);

/// Constructs an unbound relationship \ref mg_value given the underlying
/// \ref mg_unbound_relationship.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_unbound_relationship(
    mg_unbound_relationship *rel);

/// Constructs a path \ref mg_value given the underlying \ref mg_path.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_path(mg_path *path);

/// Constructs a date \ref mg_value given the underlying \ref mg_date.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_date(mg_date *date);

/// Constructs a time \ref mg_value given the underlying \ref mg_time.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_time(mg_time *time);

/// Constructs a local time \ref mg_value given the underlying \ref
/// mg_local_time.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_local_time(mg_local_time *local_time);

/// Constructs a date and time \ref mg_value given the underlying \ref
/// mg_date_time.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_date_time(mg_date_time *date_time);

/// Constructs a date and time \ref mg_value given the underlying \ref
/// mg_date_time_zone_id.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_date_time_zone_id(
    mg_date_time_zone_id *date_time_zone_id);

/// Constructs a local date and time \ref mg_value given the underlying \ref
/// mg_local_date_time.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_local_date_time(
    mg_local_date_time *local_date_time);

/// Constructs a duration \ref mg_value given the underlying \ref mg_duration.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_duration(mg_duration *duration);

/// Constructs a 2D point \ref mg_value given the underlying \ref mg_point_2d.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_point_2d(mg_point_2d *point_2d);

/// Constructs a 3D point \ref mg_value given the underlying \ref mg_point_3d.
///
/// \return Pointer to the newly constructed value or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_make_point_3d(mg_point_3d *point_3d);

/// Returns the type of the given \ref mg_value.
MGCLIENT_EXPORT enum mg_value_type mg_value_get_type(const mg_value *val);

/// Returns non-zero value if value contains true, zero otherwise.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT int mg_value_bool(const mg_value *val);

/// Returns the underlying integer value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT int64_t mg_value_integer(const mg_value *val);

/// Returns the underlying float value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT double mg_value_float(const mg_value *val);

/// Returns the underlying \ref mg_string value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_string *mg_value_string(const mg_value *val);

/// Returns the underlying \ref mg_list value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_list *mg_value_list(const mg_value *val);

/// Returns the underlying \ref mg_map value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_map *mg_value_map(const mg_value *val);

/// Returns the underlying \ref mg_node value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_node *mg_value_node(const mg_value *val);

/// Returns the underlying \ref mg_relationship value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_relationship *mg_value_relationship(
    const mg_value *val);

/// Returns the underlying \ref mg_unbound_relationship value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_unbound_relationship *mg_value_unbound_relationship(
    const mg_value *val);

/// Returns the underlying \ref mg_path value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_path *mg_value_path(const mg_value *val);

/// Returns the underlying \ref mg_date value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_date *mg_value_date(const mg_value *val);

/// Returns the underlying \ref mg_time value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_time *mg_value_time(const mg_value *val);

/// Returns the underlying \ref mg_local_time value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_local_time *mg_value_local_time(const mg_value *val);

/// Returns the underlying \ref mg_date_time value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_date_time *mg_value_date_time(const mg_value *val);

/// Returns the underlying \ref mg_date_time_zone_id value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_date_time_zone_id *mg_value_date_time_zone_id(
    const mg_value *val);

/// Returns the underlying \ref mg_local_date_time value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_local_date_time *mg_value_local_date_time(
    const mg_value *val);

/// Returns the underlying \ref mg_duration value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_duration *mg_value_duration(const mg_value *val);

/// Returns the underlying \ref mg_point_2d value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_point_2d *mg_value_point_2d(const mg_value *val);

/// Returns the underlying \ref mg_point_3d value.
///
/// Type check should be made first. Accessing the wrong value results in
/// undefined behavior.
MGCLIENT_EXPORT const mg_point_3d *mg_value_point_3d(const mg_value *val);

/// Creates a copy of the given value.
///
/// \return Pointer to the copy or NULL if error occurred.
MGCLIENT_EXPORT mg_value *mg_value_copy(const mg_value *val);

/// Destroys the given value.
MGCLIENT_EXPORT void mg_value_destroy(mg_value *val);

/// Constructs a string given a null-terminated string.
///
/// A new buffer of appropriate length will be allocated and the given string
/// will be copied there.
///
/// \param str A null-terminated UTF-8 string.
///
/// \return A pointer to the newly constructed `mg_string` object or \c NULL
///         if an error occurred.
MGCLIENT_EXPORT mg_string *mg_string_make(const char *str);

/// Constructs a string given its length (in bytes) and contents.
///
/// A new buffer of will be allocated and the given data will be copied there.
///
/// \param len  Number of bytes in the data buffer.
/// \param data The string contents.
///
/// \return A pointer to the newly constructed `mg_string` object or \c NULL
///         if an error occurred.
MGCLIENT_EXPORT mg_string *mg_string_make2(uint32_t len, const char *data);

/// Returns a pointer to the beginning of data buffer of string \p str.
MGCLIENT_EXPORT const char *mg_string_data(const mg_string *str);

/// Returns the length (in bytes) of string \p str.
MGCLIENT_EXPORT uint32_t mg_string_size(const mg_string *str);

/// Creates a copy of the given string.
///
/// \return A pointer to the copy or NULL if an error occurred.
MGCLIENT_EXPORT mg_string *mg_string_copy(const mg_string *str);

/// Destroys the given string.
MGCLIENT_EXPORT void mg_string_destroy(mg_string *str);

/// Constructs a list that can hold at most \p capacity elements.
///
/// Elements should be constructed and then inserted using \ref mg_list_append.
///
/// \param capacity The maximum number of elements that the newly constructed
///                 list can hold.
///
/// \return A pointer to the newly constructed empty list or NULL if an error
///         occurred.
MGCLIENT_EXPORT mg_list *mg_list_make_empty(uint32_t capacity);

/// Appends an element at the end of the list \p list.
///
/// Insertion will fail if the list capacity is already exhausted. If the
/// insertion fails, the map doesn't take ownership of \p value.
///
/// \param list  The list instance to be modified.
/// \param value The value to be appended.
///
/// \return The function returns non-zero value if insertion failed, zero
///         otherwise.
MGCLIENT_EXPORT int mg_list_append(mg_list *list, mg_value *value);

/// Returns the number of elements in list \p list.
MGCLIENT_EXPORT uint32_t mg_list_size(const mg_list *list);

/// Retrieves the element at position \p pos in list \p list.
///
/// \return A pointer to required list element. If \p pos is outside of list
///         bounds, \c NULL is returned.
MGCLIENT_EXPORT const mg_value *mg_list_at(const mg_list *list, uint32_t pos);

/// Creates a copy of the given list.
///
/// \return A pointer to the copy or NULL if an error occurred.
MGCLIENT_EXPORT mg_list *mg_list_copy(const mg_list *list);

/// Destroys the given list.
MGCLIENT_EXPORT void mg_list_destroy(mg_list *list);

/// Constructs an empty map that can hold at most \p capacity key-value pairs.
///
/// Key-value pairs should be constructed and then inserted using
/// \ref mg_map_insert, \ref mg_map_insert_unsafe and similar.
///
/// \param capacity The maximum number of key-value pairs that the newly
///                 constructed list can hold.
///
/// \return A pointer to the newly constructed empty map or NULL if an error
///         occurred.
MGCLIENT_EXPORT mg_map *mg_map_make_empty(uint32_t capacity);

/// Inserts the given key-value pair into the map.
///
/// A check is performed to see if the given key is unique in the map which
/// means that a number of key comparisons equal to the current number of
/// elements in the map is made.
///
/// If key length is greater that \c UINT32_MAX, or the key already exists in
/// map, or the map's capacity is exhausted, the insertion will fail. If
/// insertion fails, the map doesn't take ownership of \p value.
///
/// If the insertion is successful, a new \ref mg_string is constructed for
/// the storage of the key and the map takes ownership of \p value.
///
/// \param map     The map instance to be modifed.
/// \param key_str A null-terminated string to be used as key.
/// \param value   Value to be inserted.
///
/// \return The function returns non-zero value if insertion failed, zero
///         otherwise.
MGCLIENT_EXPORT int mg_map_insert(mg_map *map, const char *key_str,
                                  mg_value *value);

/// Inserts the given key-value pair into the map.
///
/// A check is performed to see if the given key is unique in the map which
/// means that a number of key comparisons equal to the current number of
/// elements in the map is made.
///
/// If the key already exists in map, or the map's capacity is exhausted, the
/// insertion will fail. If insertion fails, the map doesn't take ownership of
/// \p key and \p value.
///
/// If the insertion is successful, map takes ownership of \p key and \p value.
///
/// \param map     The map instance to be modifed.
/// \param key     A \ref mg_string to be used as key.
/// \param value   Value to be inserted.
///
/// \return The function returns non-zero value if insertion failed, zero
///         otherwise.
MGCLIENT_EXPORT int mg_map_insert2(mg_map *map, mg_string *key,
                                   mg_value *value);

/// Inserts the given key-value pair into the map.
///
/// No check is performed for key uniqueness. Note that map containing duplicate
/// keys is considered invalid in Bolt protocol.
///
/// If key length is greated than \c UINT32_MAX or or the map's capacity is
/// exhausted, the insertion will fail. If insertion fails, the map doesn't take
/// ownership of \p value.
///
/// If the insertion is successful, a new \ref mg_string is constructed for the
/// storage of the key and the map takes ownership of \p value.
///
/// \param map     The map instance to be modifed.
/// \param key_str A null-terminated string to be used as key.
/// \param value   Value to be inserted.
///
/// \return The function returns non-zero value if insertion failed, zero
///         otherwise.
MGCLIENT_EXPORT int mg_map_insert_unsafe(mg_map *map, const char *key_str,
                                         mg_value *value);

/// Inserts the given key-value pair into the map.
///
/// No check is performed for key uniqueness. Note that map containing duplicate
/// keys is considered invalid in Bolt protocol.
///
/// If the map's capacity is exhausted, the insertion will fail. If insertion
/// fails, the map doesn't take ownership of \p key and \p value.
///
/// If the insertion is successful, map takes ownership of \p key and \p value.
///
/// \param map     The map instance to be modifed.
/// \param key     A \ref mg_string to be used as key.
/// \param value   Value to be inserted.
///
/// \return The function returns non-zero value if insertion failed, zero
///         otherwise.
MGCLIENT_EXPORT int mg_map_insert_unsafe2(mg_map *map, mg_string *key,
                                          mg_value *value);

/// Looks up a map value with the given key.
///
/// \param map     The map instance to be queried.
/// \param key_str A null-terminated string representing the key to be looked-up
///                in the map.
///
/// \return If the key is found in the map, the pointer to the corresponding
///         \ref mg_value is returned. Otherwise, \c NULL is returned.
MGCLIENT_EXPORT const mg_value *mg_map_at(const mg_map *map,
                                          const char *key_str);

/// Looks up a map value with the given key.
///
/// \param map      The map instance to be queried.
/// \param key_size The length of the string representing the key to be
///                 looked-up in the map.
/// \param key_data Bytes constituting the key string.
///
/// \return If the key is found in the map, the pointer to the corresponding
///         \ref mg_value is returned. Otherwise, \c NULL is returned.
MGCLIENT_EXPORT const mg_value *mg_map_at2(const mg_map *map, uint32_t key_size,
                                           const char *key_data);

/// Returns the number of key-value pairs in map \p map.
MGCLIENT_EXPORT uint32_t mg_map_size(const mg_map *map);

/// Retrieves the key at position \p pos in map \p map.
///
/// \return A pointer to required key. If \p pos is outside of map bounds, \c
///         NULL is returned.
MGCLIENT_EXPORT const mg_string *mg_map_key_at(const mg_map *, uint32_t pos);

/// Retrieves the value at position \p pos in map \p map.
///
/// \return A pointer to required value. If \p pos is outside of map bounds,
///         \c NULL is returned.
MGCLIENT_EXPORT const mg_value *mg_map_value_at(const mg_map *, uint32_t pos);

/// Creates a copy of the given map.
///
/// \return A pointer to the copy or NULL if an error occurred.
MGCLIENT_EXPORT mg_map *mg_map_copy(const mg_map *map);

/// Destroys the given map.
MGCLIENT_EXPORT void mg_map_destroy(mg_map *map);

/// Returns the ID of node \p node.
MGCLIENT_EXPORT int64_t mg_node_id(const mg_node *node);

/// Returns the number of labels of node \p node.
MGCLIENT_EXPORT uint32_t mg_node_label_count(const mg_node *node);

/// Returns the label at position \p pos in node \p node's label list.
///
/// \return A pointer to the required label. If \p pos is outside of label list
///         bounds, \c NULL is returned.
MGCLIENT_EXPORT const mg_string *mg_node_label_at(const mg_node *node,
                                                  uint32_t pos);

/// Returns property map of node \p node.
MGCLIENT_EXPORT const mg_map *mg_node_properties(const mg_node *node);

/// Creates a copy of the given node.
///
/// \return A pointer to the copy or NULL if an error occurred.
MGCLIENT_EXPORT mg_node *mg_node_copy(const mg_node *node);

/// Destroys the given node.
MGCLIENT_EXPORT void mg_node_destroy(mg_node *node);

/// Returns the ID of the relationship \p rel.
MGCLIENT_EXPORT int64_t mg_relationship_id(const mg_relationship *rel);

/// Returns the ID of the start node of relationship \p rel.
MGCLIENT_EXPORT int64_t mg_relationship_start_id(const mg_relationship *rel);

/// Returns the ID of the end node of relationship \p rel.
MGCLIENT_EXPORT int64_t mg_relationship_end_id(const mg_relationship *rel);

/// Returns the type of the relationship \p rel.
MGCLIENT_EXPORT const mg_string *mg_relationship_type(
    const mg_relationship *rel);

/// Returns the property map of the relationship \p rel.
MGCLIENT_EXPORT const mg_map *mg_relationship_properties(
    const mg_relationship *rel);

/// Creates a copy of the given relationship.
///
/// \return A pointer to the copy or NULL if an error occurred.
MGCLIENT_EXPORT mg_relationship *mg_relationship_copy(
    const mg_relationship *rel);

/// Destroys the given relationship.
MGCLIENT_EXPORT void mg_relationship_destroy(mg_relationship *rel);

/// Returns the ID of the unbound relationship \p rel.
MGCLIENT_EXPORT int64_t
mg_unbound_relationship_id(const mg_unbound_relationship *rel);

/// Returns the type of the unbound relationship \p rel.
MGCLIENT_EXPORT const mg_string *mg_unbound_relationship_type(
    const mg_unbound_relationship *rel);

/// Returns the property map of the unbound relationship \p rel.
MGCLIENT_EXPORT const mg_map *mg_unbound_relationship_properties(
    const mg_unbound_relationship *rel);

/// Creates a copy of the given unbound relationship.
///
/// \return A pointer to the copy or NULL if an error occurred.
MGCLIENT_EXPORT mg_unbound_relationship *mg_unbound_relationship_copy(
    const mg_unbound_relationship *rel);

/// Destroys the given unbound relationship.
MGCLIENT_EXPORT void mg_unbound_relationship_destroy(
    mg_unbound_relationship *rel);

/// Returns the length (the number of edges) of path \p path.
MGCLIENT_EXPORT uint32_t mg_path_length(const mg_path *path);

/// Returns the node at position \p pos in the traversal of path \p path.
///
/// Nodes are indexed from 0 to path length.
///
/// \return A pointer to the required node. If \p pos is out of path bounds, \c
///         NULL is returned.
MGCLIENT_EXPORT const mg_node *mg_path_node_at(const mg_path *path,
                                               uint32_t pos);

/// Returns the relationship at position \p pos in traversal of path \p path.
///
/// Relationships are indexed from 0 to path length - 1.
///
/// \return A pointer to the required relationship. If \p pos is outside of
///         path bounds, \c NULL is returned.
MGCLIENT_EXPORT const mg_unbound_relationship *mg_path_relationship_at(
    const mg_path *path, uint32_t pos);

/// Checks if the relationship at position \p pos in traversal of path \p path
/// is reversed.
///
/// Relationships are indexed from 0 to path length - 1.
///
/// \return Returns 0 if relationships is traversed in the same direction as the
///         underlying relationship in the data graph, and 1 if it is traversed
///         in the opposite direction. If \p pos is outside of path bounds, -1
///         is returned.
MGCLIENT_EXPORT int mg_path_relationship_reversed_at(const mg_path *path,
                                                     uint32_t pos);

/// Creates a copy of the given path.
///
/// \return A pointer to the copy or NULL if an error occurred.
MGCLIENT_EXPORT mg_path *mg_path_copy(const mg_path *path);

/// Destroys the given path.
MGCLIENT_EXPORT void mg_path_destroy(mg_path *path);

/// Creates mg_date from days.
/// \return a pointer to mg_date or NULL if an error occurred.
MGCLIENT_EXPORT mg_date *mg_date_make(int64_t days);

/// Returns days since the Unix epoch.
MGCLIENT_EXPORT int64_t mg_date_days(const mg_date *date);

/// Creates a copy of the given date.
///
/// \return A pointer to the copy or NULL if an error occured.
MGCLIENT_EXPORT mg_date *mg_date_copy(const mg_date *date);

/// Destroys the given date.
MGCLIENT_EXPORT void mg_date_destroy(mg_date *date);

/// Returns nanoseconds since midnight.
MGCLIENT_EXPORT int64_t mg_time_nanoseconds(const mg_time *time);

/// Returns time zone offset in seconds from UTC.
MGCLIENT_EXPORT int64_t mg_time_tz_offset_seconds(const mg_time *time);

/// Creates a copy of the given time.
///
/// \return A pointer to the copy or NULL if an error occured.
MGCLIENT_EXPORT mg_time *mg_time_copy(const mg_time *time);

/// Destroys the given time.
MGCLIENT_EXPORT void mg_time_destroy(mg_time *time);

/// Returns nanoseconds since midnight.
MGCLIENT_EXPORT int64_t
mg_local_time_nanoseconds(const mg_local_time *local_time);

/// Creates mg_local_time from nanoseconds.
/// \return a pointer to mg_local_time or NULL if an error occurred.
MGCLIENT_EXPORT mg_local_time *mg_local_time_make(int64_t nanoseconds);

/// Creates a copy of the given local time.
///
/// \return A pointer to the copy or NULL if an error occured.
MGCLIENT_EXPORT mg_local_time *mg_local_time_copy(
    const mg_local_time *local_time);

/// Destroys the given local time.
MGCLIENT_EXPORT void mg_local_time_destroy(mg_local_time *local_time);

/// Returns seconds since Unix epoch.
MGCLIENT_EXPORT int64_t mg_date_time_seconds(const mg_date_time *date_time);

/// Returns nanoseconds since midnight.
MGCLIENT_EXPORT int64_t mg_date_time_nanoseconds(const mg_date_time *date_time);

/// Returns time zone offset in minutes from UTC.
MGCLIENT_EXPORT int64_t
mg_date_time_tz_offset_minutes(const mg_date_time *date_time);

/// Creates a copy of the given date and time.
///
/// \return A pointer to the copy or NULL if an error occured.
MGCLIENT_EXPORT mg_date_time *mg_date_time_copy(const mg_date_time *date_time);

/// Destroys the given date and time.
MGCLIENT_EXPORT void mg_date_time_destroy(mg_date_time *date_time);

/// Returns seconds since Unix epoch.
MGCLIENT_EXPORT int64_t
mg_date_time_zone_id_seconds(const mg_date_time_zone_id *date_time_zone_id);

/// Returns nanoseconds since midnight.
MGCLIENT_EXPORT int64_t
mg_date_time_zone_id_nanoseconds(const mg_date_time_zone_id *date_time_zone_id);

/// Returns time zone represented by the identifier.
MGCLIENT_EXPORT int64_t
mg_date_time_zone_id_tz_id(const mg_date_time_zone_id *date_time_zone_id);

/// Creates a copy of the given date and time.
///
/// \return A pointer to the copy or NULL if an error occured.
MGCLIENT_EXPORT mg_date_time_zone_id *mg_date_time_zone_id_copy(
    const mg_date_time_zone_id *date_time_zone_id);

/// Destroys the given date and time.
MGCLIENT_EXPORT void mg_date_time_zone_id_destroy(
    mg_date_time_zone_id *date_time_zone_id);

/// Creates mg_local_date_time from seconds and nanoseconds.
/// \return a pointer to mg_local_date_time or NULL if an error occurred.
MGCLIENT_EXPORT mg_local_date_time *mg_local_date_time_make(
    int64_t seconds, int64_t nanoseconds);
//
/// Returns seconds since Unix epoch. This includes the hours, minutes, seconds
/// fields of the local_time.
MGCLIENT_EXPORT int64_t
mg_local_date_time_seconds(const mg_local_date_time *local_date_time);

/// Returns subseconds of the local_time field as nanoseconds.
MGCLIENT_EXPORT int64_t
mg_local_date_time_nanoseconds(const mg_local_date_time *local_date_time);

/// Creates a copy of the given local date and time.
///
/// \return A pointer to the copy or NULL if an error occured.
MGCLIENT_EXPORT mg_local_date_time *mg_local_date_time_copy(
    const mg_local_date_time *local_date_time);

/// Destroy the given local date and time.
MGCLIENT_EXPORT void mg_local_date_time_destroy(
    mg_local_date_time *local_date_time);

/// Creates mg_duration from months, days, seconds and nanoseconds.
/// \return a pointer to mg_duration or NULL if an error occurred.
MGCLIENT_EXPORT mg_duration *mg_duration_make(int64_t months, int64_t days,
                                              int64_t seconds,
                                              int64_t nanoseconds);

/// Returns the months part of the temporal amount.
MGCLIENT_EXPORT int64_t mg_duration_months(const mg_duration *duration);

/// Returns the days part of the temporal amount.
MGCLIENT_EXPORT int64_t mg_duration_days(const mg_duration *duration);

/// Returns the seconds part of the temporal amount.
MGCLIENT_EXPORT int64_t mg_duration_seconds(const mg_duration *duration);

/// Returns the nanoseconds part of the temporal amount.
MGCLIENT_EXPORT int64_t mg_duration_nanoseconds(const mg_duration *duration);

/// Creates a copy of the given duration.
///
/// \return A pointer to the copy or NULL if an error occured.
MGCLIENT_EXPORT mg_duration *mg_duration_copy(const mg_duration *duration);

/// Destroy the given duration.
MGCLIENT_EXPORT void mg_duration_destroy(mg_duration *duration);

/// Returns SRID of the 2D point.
MGCLIENT_EXPORT int64_t mg_point_2d_srid(const mg_point_2d *point_2d);

/// Returns the x coordinate of the 2D point.
MGCLIENT_EXPORT double mg_point_2d_x(const mg_point_2d *point_2d);

/// Returns the y coordinate of the 2D point.
MGCLIENT_EXPORT double mg_point_2d_y(const mg_point_2d *point_2d);

/// Creates a copy of the given 2D point.
///
/// \return A pointer to the copy or NULL if an error occured.
MGCLIENT_EXPORT mg_point_2d *mg_point_2d_copy(const mg_point_2d *point_2d);

/// Destroys the given 2D point.
MGCLIENT_EXPORT void mg_point_2d_destroy(mg_point_2d *point_2d);

/// Returns SRID of the 3D point.
MGCLIENT_EXPORT int64_t mg_point_3d_srid(const mg_point_3d *point_3d);

/// Returns the x coordinate of the 3D point.
MGCLIENT_EXPORT double mg_point_3d_x(const mg_point_3d *point_3d);

/// Returns the y coordinate of the 3D point.
MGCLIENT_EXPORT double mg_point_3d_y(const mg_point_3d *point_3d);

/// Returns the z coordinate of the 3D point.
MGCLIENT_EXPORT double mg_point_3d_z(const mg_point_3d *point_3d);

/// Creates a copy of the given 3D point.
///
/// \return A pointer to the copy or NULL if an error occured.
MGCLIENT_EXPORT mg_point_3d *mg_point_3d_copy(const mg_point_3d *point_3d);

/// Destroys the given 3D point.
MGCLIENT_EXPORT void mg_point_3d_destroy(mg_point_3d *point_3d);

/// Marks a \ref mg_session ready to execute a new query using \ref
/// mg_session_run.
#define MG_SESSION_READY 0

/// Marks a \ref mg_session which is currently executing a query. Results can be
/// pulled using \ref mg_session_pull.
#define MG_SESSION_EXECUTING 1

/// Marks a bad \ref mg_session which cannot be used to execute queries and can
/// only be destroyed.
#define MG_SESSION_BAD 2

/// Marks a \ref mg_session which is currently fetching result of a query.
/// Results can be fetched using \ref mg_session_fetch.
#define MG_SESSION_FETCHING 3

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

/// Determines whether a secure SSL TCP/IP connection will be negotiated with
/// the server.
enum mg_sslmode {
  MG_SSLMODE_DISABLE,  ///< Only try a non-SSL connection.
  MG_SSLMODE_REQUIRE,  ///< Only try a SSL connection.
};

/// An object encapsulating a Bolt session.
typedef struct mg_session mg_session;

/// An object containing parameters for `mg_connect`.
///
/// Currently recognized parameters are:
///  - host
///
///      DNS resolvable name of host to connect to. Exactly one of host and
///      address parameters must be specified.
///
///  - address
///
///      Numeric IP address of host to connect to. This should be in the
///      standard IPv4 address format. You can also use IPv6 if your machine
///      supports it. Exactly one of host and address parameters must be
///      specified.
///
///  - port
///
///      Port number to connect to at the server host.
///
///  - username
///
///      Username to connect as.
///
///  - password
///
///      Password to be used if the server demands password authentication.
///
///  - user_agent
///
///      Alternate name and version of the client to send to server. Default is
///      "MemgraphBolt/0.1".
///
///  - sslmode
///
///      This option determines whether a secure connection will be negotiated
///      with the server. There are 2 possible values:
///
///      - \ref MG_SSLMODE_DISABLE
///
///        Only try a non-SSL connection (default).
///
///      - \ref MG_SSLMODE_REQUIRE
///
///        Only try an SSL connection.
///
///  - sslcert
///
///      This parameter specifies the file name of the client SSL certificate.
///      It is ignored in case an SSL connection is not made.
///
///  - sslkey
///
///     This parameter specifies the location of the secret key used for the
///     client certificate. This parameter is ignored in case an SSL connection
///     is not made.
///
///  - trust_callback
///
///     A pointer to a function of prototype:
///        int trust_callback(const char *hostname, const char *ip_address,
///                           const char *key_type, const char *fingerprint,
///                           void *trust_data);
///
///     After performing the SSL handshake, \ref mg_connect will call this
///     function providing the hostname, IP address, public key type and
///     fingerprint and user provided data. If the function returns a non-zero
///     value, SSL connection will be immediately terminated. This can be used
///     to implement TOFU (trust on first use) mechanism.
///     It might happen that hostname can not be determined, in that case the
///     trust callback will be called with hostname="undefined".
///
///  - trust_data
///
///    Additional data that will be provided to trust_callback function.
typedef struct mg_session_params mg_session_params;

/// Prototype of the callback function for verifying an SSL connection by user.
typedef int (*mg_trust_callback_type)(const char *, const char *, const char *,
                                      const char *, void *);

/// Creates a new `mg_session_params` object.
MGCLIENT_EXPORT mg_session_params *mg_session_params_make();

/// Destroys a `mg_session_params` object.
MGCLIENT_EXPORT void mg_session_params_destroy(mg_session_params *);

/// Getters and setters for `mg_session_params` values.
MGCLIENT_EXPORT void mg_session_params_set_address(mg_session_params *,
                                                   const char *address);
MGCLIENT_EXPORT void mg_session_params_set_host(mg_session_params *,
                                                const char *host);
MGCLIENT_EXPORT void mg_session_params_set_port(mg_session_params *,
                                                uint16_t port);
MGCLIENT_EXPORT void mg_session_params_set_username(mg_session_params *,
                                                    const char *username);
MGCLIENT_EXPORT void mg_session_params_set_password(mg_session_params *,
                                                    const char *password);
MGCLIENT_EXPORT void mg_session_params_set_user_agent(mg_session_params *,
                                                      const char *user_agent);
MGCLIENT_EXPORT void mg_session_params_set_sslmode(mg_session_params *,
                                                   enum mg_sslmode sslmode);
MGCLIENT_EXPORT void mg_session_params_set_sslcert(mg_session_params *,
                                                   const char *sslcert);
MGCLIENT_EXPORT void mg_session_params_set_sslkey(mg_session_params *,
                                                  const char *sslkey);
MGCLIENT_EXPORT void mg_session_params_set_trust_callback(
    mg_session_params *, mg_trust_callback_type trust_callback);
MGCLIENT_EXPORT void mg_session_params_set_trust_data(mg_session_params *,
                                                      void *trust_data);

MGCLIENT_EXPORT const char *mg_session_params_get_address(
    const mg_session_params *);
MGCLIENT_EXPORT const char *mg_session_params_get_host(
    const mg_session_params *);
MGCLIENT_EXPORT uint16_t mg_session_params_get_port(const mg_session_params *);
MGCLIENT_EXPORT const char *mg_session_params_get_username(
    const mg_session_params *);
MGCLIENT_EXPORT const char *mg_session_params_get_password(
    const mg_session_params *);
MGCLIENT_EXPORT const char *mg_session_params_get_user_agent(
    const mg_session_params *);
MGCLIENT_EXPORT enum mg_sslmode mg_session_params_get_sslmode(
    const mg_session_params *);
MGCLIENT_EXPORT const char *mg_session_params_get_sslcert(
    const mg_session_params *);
MGCLIENT_EXPORT const char *mg_session_params_get_sslkey(
    const mg_session_params *);
MGCLIENT_EXPORT mg_trust_callback_type
mg_session_params_get_trust_callback(const mg_session_params *params);
MGCLIENT_EXPORT void *mg_session_params_get_trust_data(
    const mg_session_params *);

/// Makes a new connection to the database server.
///
/// This function opens a new database connection using the parameters specified
/// in provided \p params argument.
///
/// \param[in]  params  New Bolt connection parameters. See documentation for
///                     \ref mg_session_params.
/// \param[out] session A pointer to a newly created \ref mg_session is written
///                     here, unless there wasn't enough memory to allocate a
///                     \ref mg_session object. In that case, it is set to NULL.
///
/// \return Returns 0 if connected successfuly, otherwise returns a non-zero
///         error code. A more detailed error message can be obtained by using
///         \ref mg_session_error on \p session, unless it is set to NULL.
MGCLIENT_EXPORT int mg_connect(const mg_session_params *params,
                               mg_session **session);

/// Returns the status of \ref mg_session.
///
/// \return One of \ref MG_SESSION_READY, \ref MG_SESSION_EXECUTING,
/// \ref MG_SESSION_BAD.
MGCLIENT_EXPORT int mg_session_status(const mg_session *session);

/// Obtains the error message stored in \ref mg_session (if any).
MGCLIENT_EXPORT const char *mg_session_error(mg_session *session);

/// Destroys a \ref mg_session and releases all of its resources.
MGCLIENT_EXPORT void mg_session_destroy(mg_session *session);

/// An object encapsulating a single result row or query execution summary. Its
/// lifetime is limited by lifetime of parent \ref mg_session. Also, invoking
/// \ref mg_session_pull ends the lifetime of previously returned \ref
/// mg_result.
typedef struct mg_result mg_result;

/// Submits a query to the server for execution.
///
/// All records from the previous query must be pulled before executing the
/// next query.
///
/// \param session               A \ref mg_session to be used for query
///                              execution.
/// \param query                 Query string.
/// \param params                A \ref mg_map containing query parameters. NULL
///                              can be supplied instead of an empty parameter
///                              map.
/// \param columns               Names of the columns output by the query
///                              execution will be stored in here. This is the
///                              same as the value
///                              obtained by \ref mg_result_columns on a pulled
///                              \ref mg_result. NULL can be supplied if we're
///                              not interested in the columns names.
/// \param extra_run_information A \ref mg_map containing extra information for
///                              running the statement.
///                              It can contain the following information:
///                               - bookmarks - list of strings containing some
///                               kind of bookmark identification
///                               - tx_timeout - integer that specifies a
///                               transaction timeout in ms.
///                               - tx_metadata - dictionary taht can contain
///                               some metadata information, mainly used for
///                               logging.
///                               - mode - specifies what kind of server is the
///                               run targeting. For write access use "w" and
///                               for read access use "r". Defaults to write
///                               access.
///                               - db - specifies the database name for
///                               multi-database to select where the transaction
///                               takes place. If no `db` is sent or empty
///                               string it implies that it is the default
///                               database.
/// \param qid                    QID for the statement will be stored in here
///                               if an Explicit transaction was started.
/// \return Returns 0 if query was submitted for execution successfuly.
///         Otherwise, a non-zero error code is returned.
MGCLIENT_EXPORT int mg_session_run(mg_session *session, const char *query,
                                   const mg_map *params,
                                   const mg_map *extra_run_information,
                                   const mg_list **columns, int64_t *qid);

/// Starts an Explicit transaction on the server.
///
/// Every run will be part of that transaction until its explicitly ended.
///
/// \param session               A \ref mg_session on which the transaction
/// should be started. \param extra_run_information A \ref mg_map containing
/// extra information that will be used for every statement that is ran as part
/// of the transaction.
///                              It can contain the following information:
///                               - bookmarks - list of strings containing some
///                               kind of bookmark identification
///                               - tx_timeout - integer that specifies a
///                               transaction timeout in ms.
///                               - tx_metadata - dictionary taht can contain
///                               some metadata information, mainly used for
///                               logging.
///                               - mode - specifies what kind of server is the
///                               run targeting. For write access use "w" and
///                               for read access use "r". Defaults to write
///                               access.
///                               - db - specifies the database name for
///                               multi-database to select where the transaction
///                               takes place. If no `db` is sent or empty
///                               string it implies that it is the default
///                               database.
/// \return Returns 0 if the transaction was started successfuly.
///         Otherwise, a non-zero error code is returned.
MGCLIENT_EXPORT int mg_session_begin_transaction(
    mg_session *session, const mg_map *extra_run_information);

/// Commits current Explicit transaction.
///
/// \param session A \ref mg_session on which the transaction should
///                be commited.
/// \param result  Contains the information about the commited transaction
///                if it was successful.
/// \return Returns 0 if the  transaction was ended successfuly.
///         Otherwise, a non-zero error code is returned.
MGCLIENT_EXPORT int mg_session_commit_transaction(mg_session *session,
                                                  mg_result **result);

/// Rollbacks current Explicit transaction.
///
/// \param session A \ref mg_session on which the transaction should
///                be rollbacked.
/// \param result  Contains the information about the rollbacked transaction
///                if it was successful.
/// \return Returns 0 if the transaction was ended successfuly.
///         Otherwise, a non-zero error code is returned.
MGCLIENT_EXPORT int mg_session_rollback_transaction(mg_session *session,
                                                    mg_result **result);

/// Tries to fetch the next query result from \ref mg_session.
///
/// The owner of the returned result is \ref mg_session \p session, and the
/// result is destroyed on next call to \ref mg_session_fetch.
///
/// \return On success, 0 or 1 is returned. Exit code 1 means that a new result
///         row was obtained and stored in \p result and its contents may be
///         accessed using \ref mg_result_row. Exit code 0 means that there are
///         no more result rows and that the query execution summary was stored
///         in \p result. Its contents may be accessed using \ref
///         mg_result_summary. On failure, a non-zero exit code is returned.
MGCLIENT_EXPORT int mg_session_fetch(mg_session *session, mg_result **result);

/// Tries to pull results of a statement.
///
/// \param session          A \ref mg_session from which the results should be
///                         pulled.
/// \param pull_information A \ref mg_map that contains extra information for
///                         pulling the results.
///                         It can contain the following information:
///                          - n - how many records to fetch. `n=-1` will fetch
///                          all records.
///                          - qid - query identification, specifies the result
///                          from which statement the results should be pulled.
///                          `qid=-1` denotes the last executed statement. This
///                          is only for Explicit transactions.
/// \return Returns 0 if the result was pulled successfuly.
///         Otherwise, a non-zero error code is returned.
MGCLIENT_EXPORT int mg_session_pull(mg_session *session,
                                    const mg_map *pull_information);

/// Returns names of columns output by the current query execution.
MGCLIENT_EXPORT const mg_list *mg_result_columns(const mg_result *result);

/// Returns column values of current result row.
MGCLIENT_EXPORT const mg_list *mg_result_row(const mg_result *result);

/// Returns query execution summary.
MGCLIENT_EXPORT const mg_map *mg_result_summary(const mg_result *result);

#ifdef __cplusplus
}
#endif

#endif
