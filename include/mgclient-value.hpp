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

#include <cstring>
#include <stdexcept>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mgclient.h"

namespace mg {

namespace detail {
// uint to int conversion in C++ is a bit tricky. Take a look here
// https://stackoverflow.com/questions/14623266/why-cant-i-reinterpret-cast-uint-to-int
// for more details.
template <typename TDest, typename TSrc>
TDest MemcpyCast(TSrc src) {
  TDest dest;
  static_assert(sizeof(dest) == sizeof(src),
                "MemcpyCast expects source and destination to be of same size");
  static_assert(std::is_arithmetic<TSrc>::value,
                "MemcpyCast expects source is an arithmetic type");
  static_assert(std::is_arithmetic<TDest>::value,
                "MemcpyCast expects destination is an arithmetic type");
  std::memcpy(&dest, &src, sizeof(src));
  return dest;
}
}  // namespace detail

// Forward declarations:
class ConstList;
class ConstMap;
class ConstNode;
class ConstRelationship;
class ConstUnboundRelationship;
class ConstPath;
class ConstDate;
class ConstTime;
class ConstLocalTime;
class ConstDateTime;
class ConstDateTimeZoneId;
class ConstLocalDateTime;
class ConstDuration;
class ConstPoint2d;
class ConstPoint3d;
class ConstValue;
class Value;

#define CREATE_ITERATOR(container, element)                                    \
  class Iterator {                                                             \
   private:                                                                    \
    friend class container;                                                    \
                                                                               \
   public:                                                                     \
    bool operator==(const Iterator &other) const {                             \
      return iterable_ == other.iterable_ && index_ == other.index_;           \
    }                                                                          \
                                                                               \
    bool operator!=(const Iterator &other) const { return !(*this == other); } \
                                                                               \
    Iterator &operator++() {                                                   \
      index_++;                                                                \
      return *this;                                                            \
    }                                                                          \
                                                                               \
    element operator*() const;                                                 \
                                                                               \
   private:                                                                    \
    Iterator(const container *iterable, size_t index)                          \
        : iterable_(iterable), index_(index) {}                                \
                                                                               \
    const container *iterable_;                                                \
    size_t index_;                                                             \
  }

/// Wrapper for int64_t ID to prevent dangerous implicit conversions.
class Id {
 public:
  Id() = default;

  /// Construct Id from uint64_t
  static Id FromUint(uint64_t id) {
    return Id(detail::MemcpyCast<int64_t>(id));
  }

  /// Construct Id from int64_t
  static Id FromInt(int64_t id) { return Id(id); }

  int64_t AsInt() const { return id_; }
  uint64_t AsUint() const { return detail::MemcpyCast<uint64_t>(id_); }

  bool operator==(const Id &other) const { return id_ == other.id_; }
  bool operator!=(const Id &other) const { return !(*this == other); }

 private:
  explicit Id(int64_t id) : id_(id) {}

  int64_t id_;
};

////////////////////////////////////////////////////////////////////////////////
/// List:

/// \brief Wrapper class for \ref mg_list.
class List final {
 private:
  friend class Value;

 public:
  CREATE_ITERATOR(List, ConstValue);

  explicit List(mg_list *ptr) : ptr_(ptr) {}

  /// \brief Create a List from a copy of the given \ref mg_list.
  explicit List(const mg_list *const_ptr) : List(mg_list_copy(const_ptr)) {}

  List(const List &other);
  List(List &&other);
  List &operator=(const List &other) = delete;
  List &operator=(List &&other) = delete;

  ~List();

  /// \brief Create a new list by copying the ConstList.
  explicit List(const ConstList &list);

  /// \brief Constructs a list that can hold at most \p capacity elements.
  /// \param capacity The maximum number of elements that the newly constructed
  ///                 list can hold.
  explicit List(size_t capacity) : List(mg_list_make_empty(capacity)) {}

  explicit List(const std::vector<mg::Value> &values);

  explicit List(std::vector<mg::Value> &&values);

  List(std::initializer_list<Value> list);

  size_t size() const { return mg_list_size(ptr_); }

  bool empty() const { return size() == 0; }

  /// \brief Returns the value at the given `index`.
  const ConstValue operator[](size_t index) const;

  Iterator begin() const { return Iterator(this, 0); }
  Iterator end() const { return Iterator(this, size()); }

  /// \brief Appends the given `value` to the list.
  /// The `value` is copied.
  bool Append(const Value &value);

  /// \brief Appends the given `value` to the list.
  /// The `value` is copied.
  bool Append(const ConstValue &value);

  /// \brief Appends the given `value` to the list.
  /// \note
  /// It takes the ownership of the `value` by moving it.
  /// Behaviour of accessing the `value` after performing this operation is
  /// considered undefined.
  bool Append(Value &&value);

  const ConstList AsConstList() const;

  /// \exception std::runtime_error list contains value with unknown type
  bool operator==(const List &other) const;
  /// \exception std::runtime_error list contains value with unknown type
  bool operator==(const ConstList &other) const;
  /// \exception std::runtime_error list contains value with unknown type
  bool operator!=(const List &other) const { return !(*this == other); }
  /// \exception std::runtime_error list contains value with unknown type
  bool operator!=(const ConstList &other) const { return !(*this == other); }

  const mg_list *ptr() const { return ptr_; }

 private:
  mg_list *ptr_;
};

class ConstList final {
 public:
  CREATE_ITERATOR(ConstList, ConstValue);

  explicit ConstList(const mg_list *const_ptr) : const_ptr_(const_ptr) {}

  size_t size() const { return mg_list_size(const_ptr_); }
  bool empty() const { return size() == 0; }

  /// \brief Returns the value at the given `index`.
  const ConstValue operator[](size_t index) const;

  Iterator begin() const { return Iterator(this, 0); }
  Iterator end() const { return Iterator(this, size()); }

  /// \exception std::runtime_error list contains value with unknown type
  bool operator==(const ConstList &other) const;
  /// \exception std::runtime_error list contains value with unknown type
  bool operator==(const List &other) const;
  /// \exception std::runtime_error list contains value with unknown type
  bool operator!=(const ConstList &other) const { return !(*this == other); }
  /// \exception std::runtime_error list contains value with unknown type
  bool operator!=(const List &other) const { return !(*this == other); }

  const mg_list *ptr() const { return const_ptr_; }

 private:
  const mg_list *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Map:

/// \brief Wrapper class for \ref mg_map.
class Map final {
 private:
  friend class Value;
  using KeyValuePair = std::pair<std::string_view, ConstValue>;

 public:
  CREATE_ITERATOR(Map, KeyValuePair);

  explicit Map(mg_map *ptr) : ptr_(ptr) {}

  /// \brief Create a Map from a copy of the given \ref mg_map.
  explicit Map(const mg_map *const_ptr) : Map(mg_map_copy(const_ptr)) {}

  Map(const Map &other);
  Map(Map &&other);
  Map &operator=(const Map &other) = delete;
  Map &operator=(Map &&other) = delete;
  ~Map();

  /// Copies content of the given `map`.
  explicit Map(const ConstMap &map);

  /// \brief Constructs an empty Map that can hold at most \p capacity key-value
  /// pairs.
  ///
  /// Key-value pairs should be constructed and then inserted using
  /// \ref Insert, \ref InsertUnsafe and similar.
  ///
  /// \param capacity The maximum number of key-value pairs that the newly
  ///                 constructed Map can hold.
  explicit Map(size_t capacity) : Map(mg_map_make_empty(capacity)) {}

  /// \brief Constructs an map from the list of key-value pairs.
  /// Values are copied.
  Map(std::initializer_list<std::pair<std::string, Value>> list);

  size_t size() const { return mg_map_size(ptr_); }

  bool empty() const { return size() == 0; }

  /// \brief Returns the value associated with the given `key`.
  /// Behaves undefined if there is no such a value.
  /// \note
  /// Each key-value pair has to be checked, resulting with
  /// O(n) time complexity.
  ConstValue operator[](const std::string_view key) const;

  Iterator begin() const { return Iterator(this, 0); }
  Iterator end() const { return Iterator(this, size()); }

  /// \brief Returns the key-value iterator for the given `key`.
  /// In the case there is no such pair, `end` iterator is returned.
  /// \note
  /// Each key-value pair has to be checked, resulting with O(n) time
  /// complexity.
  Iterator find(const std::string_view key) const;

  /// \brief Inserts the given `key`-`value` pair into the map.
  /// Checks if the given `key` already exists by iterating over all entries.
  /// Copies both the `key` and the `value`.
  bool Insert(const std::string_view key, const Value &value);

  /// \brief Inserts the given `key`-`value` pair into the map.
  /// Checks if the given `key` already exists by iterating over all entries.
  /// Copies both the `key` and the `value`.
  bool Insert(const std::string_view key, const ConstValue &value);

  /// \brief Inserts the given `key`-`value` pair into the map.
  /// Checks if the given `key` already exists by iterating over all entries.
  /// Copies the `key` and takes the ownership of `value` by moving it.
  /// Behaviour of accessing the `value` after performing this operation is
  /// considered undefined.
  bool Insert(const std::string_view key, Value &&value);

  /// \brief Inserts the given `key`-`value` pair into the map.
  /// It doesn't check if the given `key` already exists in the map.
  /// Copies both the `key` and the `value`.
  bool InsertUnsafe(const std::string_view key, const Value &value);

  /// \brief Inserts the given `key`-`value` pair into the map.
  /// It doesn't check if the  given `key` already exists in the map.
  /// Copies both the `key` and the `value`.
  bool InsertUnsafe(const std::string_view key, const ConstValue &value);

  /// \brief Inserts the given `key`-`value` pair into the map.
  /// It doesn't check if the given `key` already exists in the map.
  /// Copies the `key` and takes the ownership of `value` by moving it.
  /// Behaviour of accessing the `value` after performing this operation
  /// is considered undefined.
  bool InsertUnsafe(const std::string_view key, Value &&value);

  const ConstMap AsConstMap() const;

  /// \exception std::runtime_error map contains value with unknown type
  bool operator==(const Map &other) const;
  /// \exception std::runtime_error map contains value with unknown type
  bool operator==(const ConstMap &other) const;
  /// \exception std::runtime_error map contains value with unknown type
  bool operator!=(const Map &other) const { return !(*this == other); }
  /// \exception std::runtime_error map contains value with unknown type
  bool operator!=(const ConstMap &other) const { return !(*this == other); }

  const mg_map *ptr() const { return ptr_; }

 private:
  mg_map *ptr_;
};

class ConstMap final {
 private:
  using KeyValuePair = std::pair<std::string_view, ConstValue>;

 public:
  CREATE_ITERATOR(ConstMap, KeyValuePair);

  explicit ConstMap(const mg_map *const_ptr) : const_ptr_(const_ptr) {}

  size_t size() const { return mg_map_size(const_ptr_); }

  bool empty() const { return size() == 0; }

  /// \brief Returns the value associated with the given `key`.
  /// Behaves undefined if there is no such a value.
  /// \note
  /// Each key-value pair has to be checked, resulting with O(n)
  /// time complexity.
  ConstValue operator[](const std::string_view key) const;

  Iterator begin() const { return Iterator(this, 0); }
  Iterator end() const { return Iterator(this, size()); }

  /// \brief Returns the key-value iterator for the given `key`.
  /// In the case there is no such pair, end iterator is returned.
  /// \note
  /// Each key-value pair has to be checked, resulting with O(n) time
  /// complexity.
  Iterator find(const std::string_view key) const;

  /// \exception std::runtime_error map contains value with unknown type
  bool operator==(const ConstMap &other) const;
  /// \exception std::runtime_error map contains value with unknown type
  bool operator==(const Map &other) const;
  /// \exception std::runtime_error map contains value with unknown type
  bool operator!=(const ConstMap &other) const { return !(*this == other); }
  /// \exception std::runtime_error map contains value with unknown type
  bool operator!=(const Map &other) const { return !(*this == other); }

  const mg_map *ptr() const { return const_ptr_; }

 private:
  const mg_map *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Node:

/// \brief Wrapper class for \ref mg_node
class Node final {
 private:
  friend class Value;

 public:
  /// \brief View of the node's labels
  class Labels final {
   public:
    CREATE_ITERATOR(Labels, std::string_view);

    explicit Labels(const mg_node *node) : node_(node) {}

    size_t size() const { return mg_node_label_count(node_); }

    /// \brief Return node's label at the `index` position.
    std::string_view operator[](size_t index) const;

    Iterator begin() { return Iterator(this, 0); }
    Iterator end() { return Iterator(this, size()); }

   private:
    const mg_node *node_;
  };

  explicit Node(mg_node *ptr) : ptr_(ptr) {}

  /// \brief Create a Node from a copy of the given \ref mg_node.
  explicit Node(const mg_node *const_ptr) : Node(mg_node_copy(const_ptr)) {}

  Node(const Node &other);
  Node(Node &&other);
  Node &operator=(const Node &other) = delete;
  Node &operator=(Node &&other) = delete;
  ~Node();

  explicit Node(const ConstNode &node);

  Id id() const { return Id::FromInt(mg_node_id(ptr_)); }

  Labels labels() const { return Labels(ptr_); }

  ConstMap properties() const { return ConstMap(mg_node_properties(ptr_)); }

  ConstNode AsConstNode() const;

  /// \exception std::runtime_error node property contains value with
  /// unknown type
  bool operator==(const Node &other) const;
  /// \exception std::runtime_error node property contains value with
  /// unknown type
  bool operator==(const ConstNode &other) const;
  /// \exception std::runtime_error node property contains value with
  /// unknown type
  bool operator!=(const Node &other) const { return !(*this == other); }
  /// \exception std::runtime_error node property contains value with
  /// unknown type
  bool operator!=(const ConstNode &other) const { return !(*this == other); }

  const mg_node *ptr() const { return ptr_; }

 private:
  mg_node *ptr_;
};

class ConstNode final {
 public:
  explicit ConstNode(const mg_node *const_ptr) : const_ptr_(const_ptr) {}

  Id id() const { return Id::FromInt(mg_node_id(const_ptr_)); }

  Node::Labels labels() const { return Node::Labels(const_ptr_); }

  ConstMap properties() const {
    return ConstMap(mg_node_properties(const_ptr_));
  }

  /// \exception std::runtime_error node property contains value with
  /// unknown type
  bool operator==(const ConstNode &other) const;
  /// \exception std::runtime_error node property contains value with
  /// unknown type
  bool operator==(const Node &other) const;
  /// \exception std::runtime_error node property contains value with
  /// unknown type
  bool operator!=(const ConstNode &other) const { return !(*this == other); }
  /// \exception std::runtime_error node property contains value with
  /// unknown type
  bool operator!=(const Node &other) const { return !(*this == other); }

  const mg_node *ptr() const { return const_ptr_; }

 private:
  const mg_node *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Relationship:

/// \brief Wrapper class for \ref mg_relationship.
class Relationship final {
 private:
  friend class Value;

 public:
  explicit Relationship(mg_relationship *ptr) : ptr_(ptr) {}

  /// \brief Create a Relationship from a copy the given \ref mg_relationship.
  explicit Relationship(const mg_relationship *const_ptr)
      : Relationship(mg_relationship_copy(const_ptr)) {}

  Relationship(const Relationship &other);
  Relationship(Relationship &&other);
  Relationship &operator=(const Relationship &other) = delete;
  Relationship &operator=(Relationship &&other) = delete;
  ~Relationship();

  explicit Relationship(const ConstRelationship &rel);

  Id id() const { return Id::FromInt(mg_relationship_id(ptr_)); }

  /// \brief Return the Id of the node that is at the start of the relationship.
  Id from() const { return Id::FromInt(mg_relationship_start_id(ptr_)); }

  /// \brief Return the Id of the node that is at the end of the relationship.
  Id to() const { return Id::FromInt(mg_relationship_end_id(ptr_)); }

  std::string_view type() const;

  ConstMap properties() const {
    return ConstMap(mg_relationship_properties(ptr_));
  }

  ConstRelationship AsConstRelationship() const;

  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator==(const Relationship &other) const;
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator==(const ConstRelationship &other) const;
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator!=(const Relationship &other) const { return !(*this == other); }
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator!=(const ConstRelationship &other) const {
    return !(*this == other);
  }

  const mg_relationship *ptr() const { return ptr_; }

 private:
  mg_relationship *ptr_;
};

class ConstRelationship final {
 public:
  explicit ConstRelationship(const mg_relationship *const_ptr)
      : const_ptr_(const_ptr) {}

  Id id() const { return Id::FromInt(mg_relationship_id(const_ptr_)); }

  /// \brief Return the Id of the node that is at the start of the relationship.
  Id from() const { return Id::FromInt(mg_relationship_start_id(const_ptr_)); }

  /// \brief Return the Id of the node that is at the end of the relationship.
  Id to() const { return Id::FromInt(mg_relationship_end_id(const_ptr_)); }

  std::string_view type() const;

  ConstMap properties() const {
    return ConstMap(mg_relationship_properties(const_ptr_));
  }

  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator==(const ConstRelationship &other) const;
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator==(const Relationship &other) const;
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator!=(const ConstRelationship &other) const {
    return !(*this == other);
  }
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator!=(const Relationship &other) const { return !(*this == other); }

  const mg_relationship *ptr() const { return const_ptr_; }

 private:
  const mg_relationship *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// UnboundRelationship:

/// \brief Wrapper class for \ref mg_unbound_relationship.
class UnboundRelationship final {
 private:
  friend class Value;

 public:
  explicit UnboundRelationship(mg_unbound_relationship *ptr) : ptr_(ptr) {}

  /// \brief Create an UnboundRelationship from a copy of the given
  /// \ref mg_unbound_relationship.
  explicit UnboundRelationship(const mg_unbound_relationship *const_ptr)
      : UnboundRelationship(mg_unbound_relationship_copy(const_ptr)) {}

  UnboundRelationship(const UnboundRelationship &other);
  UnboundRelationship(UnboundRelationship &&other);
  UnboundRelationship &operator=(const UnboundRelationship &other) = delete;
  UnboundRelationship &operator=(UnboundRelationship &&other) = delete;
  ~UnboundRelationship();

  explicit UnboundRelationship(const ConstUnboundRelationship &rel);

  Id id() const { return Id::FromInt(mg_unbound_relationship_id(ptr_)); }

  std::string_view type() const;

  ConstMap properties() const {
    return ConstMap(mg_unbound_relationship_properties(ptr_));
  }

  ConstUnboundRelationship AsConstUnboundRelationship() const;

  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator==(const UnboundRelationship &other) const;
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator==(const ConstUnboundRelationship &other) const;
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator!=(const UnboundRelationship &other) const {
    return !(*this == other);
  }
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator!=(const ConstUnboundRelationship &other) const {
    return !(*this == other);
  }

  const mg_unbound_relationship *ptr() const { return ptr_; }

 private:
  mg_unbound_relationship *ptr_;
};

class ConstUnboundRelationship final {
 public:
  explicit ConstUnboundRelationship(const mg_unbound_relationship *const_ptr)
      : const_ptr_(const_ptr) {}

  Id id() const { return Id::FromInt(mg_unbound_relationship_id(const_ptr_)); }

  std::string_view type() const;

  ConstMap properties() const {
    return ConstMap(mg_unbound_relationship_properties(const_ptr_));
  }

  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator==(const ConstUnboundRelationship &other) const;
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator==(const UnboundRelationship &other) const;
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator!=(const ConstUnboundRelationship &other) const {
    return !(*this == other);
  }
  /// \exception std::runtime_error relationship property contains value with
  /// unknown type
  bool operator!=(const UnboundRelationship &other) const {
    return !(*this == other);
  }

  const mg_unbound_relationship *ptr() const { return const_ptr_; }

 private:
  const mg_unbound_relationship *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Path:

/// \brief Wrapper class for \ref mg_path.
class Path final {
 private:
  friend class Value;

 public:
  explicit Path(mg_path *ptr) : ptr_(ptr) {}

  /// \brief Create a Path from a copy of the given \ref mg_path.
  explicit Path(const mg_path *const_ptr) : Path(mg_path_copy(const_ptr)) {}

  Path(const Path &other);
  Path(Path &&other);
  Path &operator=(const Path &other);
  Path &operator=(Path &&other);
  ~Path();

  explicit Path(const ConstPath &path);

  /// Length of the path is number of edges.
  size_t length() const { return mg_path_length(ptr_); }

  /// \brief Returns the vertex at the given `index`.
  /// \pre `index` should be less than or equal to length of the path.
  ConstNode GetNodeAt(size_t index) const;

  /// \brief Returns the edge at the given `index`.
  /// \pre `index` should be less than length of the path.
  ConstUnboundRelationship GetRelationshipAt(size_t index) const;

  /// \brief Returns the orientation of the edge at the given `index`.
  /// \pre `index` should be less than length of the path.
  /// \return True if the edge is reversed, false otherwise.
  bool IsReversedRelationshipAt(size_t index) const;

  ConstPath AsConstPath() const;

  /// \exception std::runtime_error path contains elements with unknown value
  bool operator==(const Path &other) const;
  /// \exception std::runtime_error path contains elements with unknown value
  bool operator==(const ConstPath &other) const;
  /// \exception std::runtime_error path contains elements with unknown value
  bool operator!=(const Path &other) const { return !(*this == other); }
  /// \exception std::runtime_error path contains elements with unknown value
  bool operator!=(const ConstPath &other) const { return !(*this == other); }

  const mg_path *ptr() const { return ptr_; }

 private:
  mg_path *ptr_;
};

class ConstPath final {
 public:
  explicit ConstPath(const mg_path *const_ptr) : const_ptr_(const_ptr) {}

  /// Length of the path in number of edges.
  size_t length() const { return mg_path_length(const_ptr_); }

  /// \brief Returns the vertex at the given `index`.
  /// \pre `index` should be less than or equal to length of the path.
  ConstNode GetNodeAt(size_t index) const;

  /// \brief Returns the edge at the given `index`.
  /// \pre `index` should be less than length of the path.
  ConstUnboundRelationship GetRelationshipAt(size_t index) const;

  /// \brief Returns the orientation of the edge at the given `index`.
  /// \pre `index` should be less than length of the path.
  /// \return True if the edge is reversed, false otherwise.
  bool IsReversedRelationshipAt(size_t index) const;

  /// \exception std::runtime_error path contains elements with unknown value
  bool operator==(const ConstPath &other) const;
  /// \exception std::runtime_error path contains elements with unknown value
  bool operator==(const Path &other) const;
  /// \exception std::runtime_error path contains elements with unknown value
  bool operator!=(const ConstPath &other) const { return !(*this == other); }
  /// \exception std::runtime_error path contains elements with unknown value
  bool operator!=(const Path &other) const { return !(*this == other); }

  const mg_path *ptr() const { return const_ptr_; }

 private:
  const mg_path *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Date:

/// \brief Wrapper class for \ref mg_date
class Date final {
 private:
  friend class Value;

 public:
  explicit Date(mg_date *ptr) : ptr_(ptr) {}

  explicit Date(const mg_date *const_ptr) : Date(mg_date_copy(const_ptr)) {}

  Date(const Date &other);
  Date(Date &&other);
  Date &operator=(const Date &other);
  Date &operator=(Date &&other);
  ~Date();

  explicit Date(const ConstDate &date);

  /// \brief Returns days since Unix epoch.
  int64_t days() const { return mg_date_days(ptr_); }

  ConstDate AsConstDate() const;

  bool operator==(const Date &other) const;
  bool operator==(const ConstDate &other) const;
  bool operator!=(const Date &other) const { return !(*this == other); }
  bool operator!=(const ConstDate &other) const { return !(*this == other); }

  const mg_date *ptr() const { return ptr_; }

 private:
  mg_date *ptr_;
};

class ConstDate final {
 public:
  explicit ConstDate(const mg_date *const_ptr) : const_ptr_(const_ptr) {}

  /// \brief Returns days since Unix epoch.
  int64_t days() const { return mg_date_days(const_ptr_); }

  bool operator==(const ConstDate &other) const;
  bool operator==(const Date &other) const;
  bool operator!=(const ConstDate &other) const { return !(*this == other); }
  bool operator!=(const Date &other) const { return !(*this == other); }

  const mg_date *ptr() const { return const_ptr_; };

 private:
  const mg_date *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Time:

/// \brief Wrapper class for \ref mg_time
class Time final {
 private:
  friend class Value;

 public:
  explicit Time(mg_time *ptr) : ptr_(ptr) {}

  explicit Time(const mg_time *const_ptr) : Time(mg_time_copy(const_ptr)) {}

  Time(const Time &other);
  Time(Time &&other);
  Time &operator=(const Time &other);
  Time &operator=(Time &&other);
  ~Time();

  explicit Time(const ConstTime &time);

  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const { return mg_time_nanoseconds(ptr_); }
  /// Returns time zone offset in seconds from UTC.
  int64_t tz_offset_seconds() const { return mg_time_tz_offset_seconds(ptr_); }

  ConstTime AsConstTime() const;

  bool operator==(const Time &other) const;
  bool operator==(const ConstTime &other) const;
  bool operator!=(const Time &other) const { return !(*this == other); }
  bool operator!=(const ConstTime &other) const { return !(*this == other); }

  const mg_time *ptr() const { return ptr_; }

 private:
  mg_time *ptr_;
};

class ConstTime final {
 public:
  explicit ConstTime(const mg_time *const_ptr) : const_ptr_(const_ptr) {}

  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const { return mg_time_nanoseconds(const_ptr_); }
  /// Returns time zone offset in seconds from UTC.
  int64_t tz_offset_seconds() const {
    return mg_time_tz_offset_seconds(const_ptr_);
  }

  bool operator==(const ConstTime &other) const;
  bool operator==(const Time &other) const;
  bool operator!=(const ConstTime &other) const { return !(*this == other); }
  bool operator!=(const Time &other) const { return !(*this == other); }

  const mg_time *ptr() const { return const_ptr_; };

 private:
  const mg_time *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// LocalTime:

/// \brief Wrapper class for \ref mg_local_time
class LocalTime final {
 private:
  friend class Value;

 public:
  explicit LocalTime(mg_local_time *ptr) : ptr_(ptr) {}

  explicit LocalTime(const mg_local_time *const_ptr)
      : LocalTime(mg_local_time_copy(const_ptr)) {}

  LocalTime(const LocalTime &other);
  LocalTime(LocalTime &&other);
  LocalTime &operator=(const LocalTime &other);
  LocalTime &operator=(LocalTime &&other);
  ~LocalTime();

  explicit LocalTime(const ConstLocalTime &local_time);

  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const { return mg_local_time_nanoseconds(ptr_); }

  ConstLocalTime AsConstLocalTime() const;

  bool operator==(const LocalTime &other) const;
  bool operator==(const ConstLocalTime &other) const;
  bool operator!=(const LocalTime &other) const { return !(*this == other); }
  bool operator!=(const ConstLocalTime &other) const {
    return !(*this == other);
  }

  const mg_local_time *ptr() const { return ptr_; }

 private:
  mg_local_time *ptr_;
};

class ConstLocalTime final {
 public:
  explicit ConstLocalTime(const mg_local_time *const_ptr)
      : const_ptr_(const_ptr) {}

  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const { return mg_local_time_nanoseconds(const_ptr_); }

  bool operator==(const ConstLocalTime &other) const;
  bool operator==(const LocalTime &other) const;
  bool operator!=(const ConstLocalTime &other) const {
    return !(*this == other);
  }
  bool operator!=(const LocalTime &other) const { return !(*this == other); }

  const mg_local_time *ptr() const { return const_ptr_; };

 private:
  const mg_local_time *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// DateTime:

/// \brief Wrapper class for \ref mg_date_time
class DateTime final {
 private:
  friend class Value;

 public:
  explicit DateTime(mg_date_time *ptr) : ptr_(ptr) {}

  explicit DateTime(const mg_date_time *const_ptr)
      : DateTime(mg_date_time_copy(const_ptr)) {}

  DateTime(const DateTime &other);
  DateTime(DateTime &&other);
  DateTime &operator=(const DateTime &other);
  DateTime &operator=(DateTime &&other);
  ~DateTime();

  explicit DateTime(const ConstDateTime &date_time);

  /// Returns seconds since Unix epoch.
  int64_t seconds() const { return mg_date_time_seconds(ptr_); }
  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const { return mg_date_time_nanoseconds(ptr_); }
  /// Returns time zone offset in minutes from UTC.
  int64_t tz_offset_minutes() const {
    return mg_date_time_tz_offset_minutes(ptr_);
  }

  ConstDateTime AsConstDateTime() const;

  bool operator==(const DateTime &other) const;
  bool operator==(const ConstDateTime &other) const;
  bool operator!=(const DateTime &other) const { return !(*this == other); }
  bool operator!=(const ConstDateTime &other) const {
    return !(*this == other);
  }

  const mg_date_time *ptr() const { return ptr_; }

 private:
  mg_date_time *ptr_;
};

class ConstDateTime final {
 public:
  explicit ConstDateTime(const mg_date_time *const_ptr)
      : const_ptr_(const_ptr) {}

  /// Returns seconds since Unix epoch.
  int64_t seconds() const { return mg_date_time_seconds(const_ptr_); }
  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const { return mg_date_time_nanoseconds(const_ptr_); }
  /// Returns time zone offset in minutes from UTC.
  int64_t tz_offset_minutes() const {
    return mg_date_time_tz_offset_minutes(const_ptr_);
  }

  bool operator==(const ConstDateTime &other) const;
  bool operator==(const DateTime &other) const;
  bool operator!=(const ConstDateTime &other) const {
    return !(*this == other);
  }
  bool operator!=(const DateTime &other) const { return !(*this == other); }

  const mg_date_time *ptr() const { return const_ptr_; };

 private:
  const mg_date_time *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// DateTimeZoneId:

/// \brief Wrapper class for \ref mg_date_time_zone_id
class DateTimeZoneId final {
 private:
  friend class Value;

 public:
  explicit DateTimeZoneId(mg_date_time_zone_id *ptr) : ptr_(ptr) {}

  explicit DateTimeZoneId(const mg_date_time_zone_id *const_ptr)
      : DateTimeZoneId(mg_date_time_zone_id_copy(const_ptr)) {}

  DateTimeZoneId(const DateTimeZoneId &other);
  DateTimeZoneId(DateTimeZoneId &&other);
  DateTimeZoneId &operator=(const DateTimeZoneId &other);
  DateTimeZoneId &operator=(DateTimeZoneId &&other);
  ~DateTimeZoneId();

  explicit DateTimeZoneId(const ConstDateTimeZoneId &date_time_zone_id);

  /// Returns seconds since Unix epoch.
  int64_t seconds() const { return mg_date_time_zone_id_seconds(ptr_); };
  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const { return mg_date_time_zone_id_nanoseconds(ptr_); }
  /// Returns time zone represented by the identifier.
  int64_t tzId() const { return mg_date_time_zone_id_tz_id(ptr_); }

  ConstDateTimeZoneId AsConstDateTimeZoneId() const;

  bool operator==(const DateTimeZoneId &other) const;
  bool operator==(const ConstDateTimeZoneId &other) const;
  bool operator!=(const DateTimeZoneId &other) const {
    return !(*this == other);
  }
  bool operator!=(const ConstDateTimeZoneId &other) const {
    return !(*this == other);
  }

  const mg_date_time_zone_id *ptr() const { return ptr_; }

 private:
  mg_date_time_zone_id *ptr_;
};

class ConstDateTimeZoneId final {
 public:
  explicit ConstDateTimeZoneId(const mg_date_time_zone_id *const_ptr)
      : const_ptr_(const_ptr) {}

  /// Returns seconds since Unix epoch.
  int64_t seconds() const { return mg_date_time_zone_id_seconds(const_ptr_); };
  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const {
    return mg_date_time_zone_id_nanoseconds(const_ptr_);
  }
  /// Returns time zone represented by the identifier.
  int64_t tzId() const { return mg_date_time_zone_id_tz_id(const_ptr_); }

  bool operator==(const ConstDateTimeZoneId &other) const;
  bool operator==(const DateTimeZoneId &other) const;
  bool operator!=(const ConstDateTimeZoneId &other) const {
    return !(*this == other);
  }
  bool operator!=(const DateTimeZoneId &other) const {
    return !(*this == other);
  }

  const mg_date_time_zone_id *ptr() const { return const_ptr_; };

 private:
  const mg_date_time_zone_id *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// LocalDateTime:

/// \brief Wrapper class for \ref mg_local_date_time
class LocalDateTime final {
 private:
  friend class Value;

 public:
  explicit LocalDateTime(mg_local_date_time *ptr) : ptr_(ptr) {}

  explicit LocalDateTime(const mg_local_date_time *const_ptr)
      : LocalDateTime(mg_local_date_time_copy(const_ptr)) {}

  LocalDateTime(const LocalDateTime &other);
  LocalDateTime(LocalDateTime &&other);
  LocalDateTime &operator=(const LocalDateTime &other);
  LocalDateTime &operator=(LocalDateTime &&other);
  ~LocalDateTime();

  explicit LocalDateTime(const ConstLocalDateTime &local_date_time);

  /// Returns seconds since Unix epoch.
  int64_t seconds() const { return mg_local_date_time_seconds(ptr_); }
  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const { return mg_local_date_time_nanoseconds(ptr_); }

  ConstLocalDateTime AsConstLocalDateTime() const;

  bool operator==(const LocalDateTime &other) const;
  bool operator==(const ConstLocalDateTime &other) const;
  bool operator!=(const LocalDateTime &other) const {
    return !(*this == other);
  }
  bool operator!=(const ConstLocalDateTime &other) const {
    return !(*this == other);
  }

  const mg_local_date_time *ptr() const { return ptr_; }

 private:
  mg_local_date_time *ptr_;
};

class ConstLocalDateTime final {
 public:
  explicit ConstLocalDateTime(const mg_local_date_time *const_ptr)
      : const_ptr_(const_ptr) {}

  /// Returns seconds since Unix epoch.
  int64_t seconds() const { return mg_local_date_time_seconds(const_ptr_); }
  /// Returns nanoseconds since midnight.
  int64_t nanoseconds() const {
    return mg_local_date_time_nanoseconds(const_ptr_);
  }

  bool operator==(const ConstLocalDateTime &other) const;
  bool operator==(const LocalDateTime &other) const;
  bool operator!=(const ConstLocalDateTime &other) const {
    return !(*this == other);
  }
  bool operator!=(const LocalDateTime &other) const {
    return !(*this == other);
  }

  const mg_local_date_time *ptr() const { return const_ptr_; };

 private:
  const mg_local_date_time *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Duration:

/// \brief Wrapper class for \ref mg_duration
class Duration final {
 private:
  friend class Value;

 public:
  explicit Duration(mg_duration *ptr) : ptr_(ptr) {}

  explicit Duration(const mg_duration *const_ptr)
      : Duration(mg_duration_copy(const_ptr)) {}

  Duration(const Duration &other);
  Duration(Duration &&other);
  Duration &operator=(const Duration &other);
  Duration &operator=(Duration &&other);
  ~Duration();

  explicit Duration(const ConstDuration &duration);

  /// Returns the months part of the temporal amount.
  int64_t months() const { return mg_duration_months(ptr_); }
  /// Returns the days part of the temporal amount.
  int64_t days() const { return mg_duration_days(ptr_); }
  /// Returns the seconds part of the temporal amount.
  int64_t seconds() const { return mg_duration_seconds(ptr_); }
  /// Returns the nanoseconds part of the temporal amount.
  int64_t nanoseconds() const { return mg_duration_nanoseconds(ptr_); }

  ConstDuration AsConstDuration() const;

  bool operator==(const Duration &other) const;
  bool operator==(const ConstDuration &other) const;
  bool operator!=(const Duration &other) const { return !(*this == other); }
  bool operator!=(const ConstDuration &other) const {
    return !(*this == other);
  }

  const mg_duration *ptr() const { return ptr_; }

 private:
  mg_duration *ptr_;
};

class ConstDuration final {
 public:
  explicit ConstDuration(const mg_duration *const_ptr)
      : const_ptr_(const_ptr) {}

  /// Returns the months part of the temporal amount.
  int64_t months() const { return mg_duration_months(const_ptr_); }
  /// Returns the days part of the temporal amount.
  int64_t days() const { return mg_duration_days(const_ptr_); }
  /// Returns the seconds part of the temporal amount.
  int64_t seconds() const { return mg_duration_seconds(const_ptr_); }
  /// Returns the nanoseconds part of the temporal amount.
  int64_t nanoseconds() const { return mg_duration_nanoseconds(const_ptr_); }

  bool operator==(const ConstDuration &other) const;
  bool operator==(const Duration &other) const;
  bool operator!=(const ConstDuration &other) const {
    return !(*this == other);
  }
  bool operator!=(const Duration &other) const { return !(*this == other); }

  const mg_duration *ptr() const { return const_ptr_; };

 private:
  const mg_duration *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Point2d:

/// \brief Wrapper class for \ref mg_point_2d
class Point2d final {
 private:
  friend class Value;

 public:
  explicit Point2d(mg_point_2d *ptr) : ptr_(ptr) {}

  explicit Point2d(const mg_point_2d *const_ptr)
      : Point2d(mg_point_2d_copy(const_ptr)) {}

  Point2d(const Point2d &other);
  Point2d(Point2d &&other);
  Point2d &operator=(const Point2d &other);
  Point2d &operator=(Point2d &&other);
  ~Point2d();

  explicit Point2d(const ConstPoint2d &point_2d);

  /// Returns SRID of the 2D point.
  int64_t srid() const { return mg_point_2d_srid(ptr_); }
  /// Returns the x coordinate of the 2D point.
  double x() const { return mg_point_2d_x(ptr_); }
  /// Returns the y coordinate of the 2D point.
  double y() const { return mg_point_2d_y(ptr_); }

  ConstPoint2d AsConstPoint2d() const;

  bool operator==(const Point2d &other) const;
  bool operator==(const ConstPoint2d &other) const;
  bool operator!=(const Point2d &other) const { return !(*this == other); }
  bool operator!=(const ConstPoint2d &other) const { return !(*this == other); }

  const mg_point_2d *ptr() const { return ptr_; }

 private:
  mg_point_2d *ptr_;
};

class ConstPoint2d final {
 public:
  explicit ConstPoint2d(const mg_point_2d *const_ptr) : const_ptr_(const_ptr) {}

  /// Returns SRID of the 2D point.
  int64_t srid() const { return mg_point_2d_srid(const_ptr_); }
  /// Returns the x coordinate of the 2D point.
  double x() const { return mg_point_2d_x(const_ptr_); }
  /// Returns the y coordinate of the 2D point.
  double y() const { return mg_point_2d_y(const_ptr_); }

  bool operator==(const ConstPoint2d &other) const;
  bool operator==(const Point2d &other) const;
  bool operator!=(const ConstPoint2d &other) const { return !(*this == other); }
  bool operator!=(const Point2d &other) const { return !(*this == other); }

  const mg_point_2d *ptr() const { return const_ptr_; };

 private:
  const mg_point_2d *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Point3d:

/// \brief Wrapper class for \ref mg_point_3d
class Point3d final {
 private:
  friend class Value;

 public:
  explicit Point3d(mg_point_3d *ptr) : ptr_(ptr) {}

  explicit Point3d(const mg_point_3d *const_ptr)
      : Point3d(mg_point_3d_copy(const_ptr)) {}

  Point3d(const Point3d &other);
  Point3d(Point3d &&other);
  Point3d &operator=(const Point3d &other);
  Point3d &operator=(Point3d &&other);
  ~Point3d();

  explicit Point3d(const ConstPoint3d &point_3d);

  /// Returns SRID of the 3D point.
  int64_t srid() const { return mg_point_3d_srid(ptr_); }
  /// Returns the x coordinate of the 3D point.
  double x() const { return mg_point_3d_x(ptr_); }
  /// Returns the y coordinate of the 3D point.
  double y() const { return mg_point_3d_y(ptr_); }
  /// Returns the z coordinate of the 3D point.
  double z() const { return mg_point_3d_z(ptr_); }

  ConstPoint3d AsConstPoint3d() const;

  bool operator==(const Point3d &other) const;
  bool operator==(const ConstPoint3d &other) const;
  bool operator!=(const Point3d &other) const { return !(*this == other); }
  bool operator!=(const ConstPoint3d &other) const { return !(*this == other); }

  const mg_point_3d *ptr() const { return ptr_; }

 private:
  mg_point_3d *ptr_;
};

class ConstPoint3d final {
 public:
  explicit ConstPoint3d(const mg_point_3d *const_ptr) : const_ptr_(const_ptr) {}

  /// Returns SRID of the 3D point.
  int64_t srid() const { return mg_point_3d_srid(const_ptr_); }
  /// Returns the x coordinate of the 3D point.
  double x() const { return mg_point_3d_x(const_ptr_); }
  /// Returns the y coordinate of the 3D point.
  double y() const { return mg_point_3d_y(const_ptr_); }
  /// Returns the z coordinate of the 3D point.
  double z() const { return mg_point_3d_z(const_ptr_); }

  bool operator==(const ConstPoint3d &other) const;
  bool operator==(const Point3d &other) const;
  bool operator!=(const ConstPoint3d &other) const { return !(*this == other); }
  bool operator!=(const Point3d &other) const { return !(*this == other); }

  const mg_point_3d *ptr() const { return const_ptr_; };

 private:
  const mg_point_3d *const_ptr_;
};

////////////////////////////////////////////////////////////////////////////////
// Value:

/// Wrapper class for \ref mg_value
class Value final {
 private:
  friend class List;
  friend class Map;

 public:
  /// \brief Types that can be stored in a `Value`.
  enum class Type : uint8_t {
    Null,
    Bool,
    Int,
    Double,
    String,
    List,
    Map,
    Node,
    Relationship,
    UnboundRelationship,
    Path,
    Date,
    Time,
    LocalTime,
    DateTime,
    DateTimeZoneId,
    LocalDateTime,
    Duration,
    Point2d,
    Point3d
  };

  /// \brief Constructs an object that becomes the owner of the given `value`.
  /// `value` is destroyed when a `Value` object is destroyed.
  explicit Value(mg_value *ptr) : ptr_(ptr) {}

  /// \brief Creates a Value from a copy of the given \ref mg_value.
  explicit Value(const mg_value *const_ptr) : Value(mg_value_copy(const_ptr)) {}

  Value(const Value &other);
  Value(Value &&other);
  Value &operator=(const Value &other) = delete;
  Value &operator=(Value &&other) = delete;
  ~Value();

  explicit Value(const ConstValue &value);

  /// \brief Creates Null value.
  Value() : Value(mg_value_make_null()) {}

  // Constructors for primitive types:
  explicit Value(bool value) : Value(mg_value_make_bool(value)) {}
  explicit Value(int value) : Value(mg_value_make_integer(value)) {}
  explicit Value(int64_t value) : Value(mg_value_make_integer(value)) {}
  explicit Value(double value) : Value(mg_value_make_float(value)) {}

  // Constructors for string:
  explicit Value(const std::string_view value);
  explicit Value(const char *value);

  /// \brief Constructs a list value and takes the ownership of the `list`.
  /// \note
  /// Behaviour of accessing the `list` after performing this operation is
  /// considered undefined.
  explicit Value(List &&list);

  /// \brief Constructs a map value and takes the ownership of the `map`.
  /// \note
  /// Behaviour of accessing the `map` after performing this operation is
  /// considered undefined.
  explicit Value(Map &&map);

  /// \brief Constructs a vertex value and takes the ownership of the given
  /// `vertex`. \note Behaviour of accessing the `vertex` after performing this
  /// operation is considered undefined.
  explicit Value(Node &&vertex);

  /// \brief Constructs an edge value and takes the ownership of the given
  /// `edge`. \note Behaviour of accessing the `edge` after performing this
  /// operation is considered undefined.
  explicit Value(Relationship &&edge);

  /// \brief Constructs an unbounded edge value and takes the ownership of the
  /// given `edge`. \note Behaviour of accessing the `edge` after performing
  /// this operation is considered undefined.
  explicit Value(UnboundRelationship &&edge);

  /// \brief Constructs a path value and takes the ownership of the given
  /// `path`. \note Behaviour of accessing the `path` after performing this
  /// operation is considered undefined.
  explicit Value(Path &&path);

  /// \brief Constructs a date value and takes the ownership of the given
  /// `date`. \note Behaviour of accessing the `date` after performing this
  /// operation is considered undefined.
  explicit Value(Date &&date);

  /// \brief Constructs a time value and takes the ownership of the given
  /// `time`. \note Behaviour of accessing the `time` after performing this
  /// operation is considered undefined.
  explicit Value(Time &&time);

  /// \brief Constructs a LocalTime value and takes the ownership of the given
  /// `localTime`. \note Behaviour of accessing the `localTime` after performing
  /// this operation is considered undefined.
  explicit Value(LocalTime &&localTime);

  /// \brief Constructs a DateTime value and takes the ownership of the given
  /// `dateTime`. \note Behaviour of accessing the `dateTime` after performing
  /// this operation is considered undefined.
  explicit Value(DateTime &&dateTime);

  /// \brief Constructs a DateTimeZoneId value and takes the ownership of the
  /// given `dateTimeZoneId`. \note Behaviour of accessing the `dateTimeZoneId`
  /// after performing this operation is considered undefined.
  explicit Value(DateTimeZoneId &&dateTimeZoneId);

  /// \brief Constructs a LocalDateTime value and takes the ownership of the
  /// given `localDateTime`. \note Behaviour of accessing the `localDateTime`
  /// after performing this operation is considered undefined.
  explicit Value(LocalDateTime &&localDateTime);

  /// \brief Constructs a Duration value and takes the ownership of the given
  /// `duration`. \note Behaviour of accessing the `duration` after performing
  /// this operation is considered undefined.
  explicit Value(Duration &&duration);

  /// \brief Constructs a Point2d value and takes the ownership of the given
  /// `point2d`. \note Behaviour of accessing the `point2d` after performing
  /// this operation is considered undefined.
  explicit Value(Point2d &&point2d);

  /// \brief Constructs a Point3d value and takes the ownership of the given
  /// `point3d`. \note Behaviour of accessing the `point3d` after performing
  /// this operation is considered undefined.
  explicit Value(Point3d &&point3d);

  /// \pre value type is Type::Bool
  bool ValueBool() const;
  /// \pre value type is Type::Int
  int64_t ValueInt() const;
  /// \pre value type is Type::Double
  double ValueDouble() const;
  /// \pre value type is Type::String
  std::string_view ValueString() const;
  /// \pre value type is Type::List
  const ConstList ValueList() const;
  /// \pre value type is Type::Map
  const ConstMap ValueMap() const;
  /// \pre value type is Type::Node
  const ConstNode ValueNode() const;
  /// \pre value type is Type::Relationship
  const ConstRelationship ValueRelationship() const;
  /// \pre value type is Type::UnboundRelationship
  const ConstUnboundRelationship ValueUnboundRelationship() const;
  /// \pre value type is Type::Path
  const ConstPath ValuePath() const;
  /// \pre value type is Type::Date
  const ConstDate ValueDate() const;
  /// \pre value type is Type::Time
  const ConstTime ValueTime() const;
  /// \pre value type is Type::LocalTime
  const ConstLocalTime ValueLocalTime() const;
  /// \pre value type is Type::DateTime
  const ConstDateTime ValueDateTime() const;
  /// \pre value type is Type::DateTimeZoneId
  const ConstDateTimeZoneId ValueDateTimeZoneId() const;
  /// \pre value type is Type::LocalDateTime
  const ConstLocalDateTime ValueLocalDateTime() const;
  /// \pre value type is Type::Duration
  const ConstDuration ValueDuration() const;
  /// \pre value type is Type::Point2d
  const ConstPoint2d ValuePoint2d() const;
  /// \pre value type is Type::Point3d
  const ConstPoint3d ValuePoint3d() const;

  /// \exception std::runtime_error the value type is unknown
  Type type() const;

  ConstValue AsConstValue() const;

  /// \exception std::runtime_error the value type is unknown
  bool operator==(const Value &other) const;
  /// \exception std::runtime_error the value type is unknown
  bool operator==(const ConstValue &other) const;
  /// \exception std::runtime_error the value type is unknown
  bool operator!=(const Value &other) const { return !(*this == other); }
  /// \exception std::runtime_error the value type is unknown
  bool operator!=(const ConstValue &other) const { return !(*this == other); }

  const mg_value *ptr() const { return ptr_; }

 private:
  mg_value *ptr_;
};

class ConstValue final {
 public:
  explicit ConstValue(const mg_value *const_ptr) : const_ptr_(const_ptr) {}

  /// \pre value type is Type::Bool
  bool ValueBool() const;
  /// \pre value type is Type::Int
  int64_t ValueInt() const;
  /// \pre value type is Type::Double
  double ValueDouble() const;
  /// \pre value type is Type::String
  std::string_view ValueString() const;
  /// \pre value type is Type::List
  const ConstList ValueList() const;
  /// \pre value type is Type::Map
  const ConstMap ValueMap() const;
  /// \pre value type is Type::Node
  const ConstNode ValueNode() const;
  /// \pre value type is Type::Relationship
  const ConstRelationship ValueRelationship() const;
  /// \pre value type is Type::UnboundRelationship
  const ConstUnboundRelationship ValueUnboundRelationship() const;
  /// \pre value type is Type::Path
  const ConstPath ValuePath() const;
  /// \pre value type is Type::Date
  const ConstDate ValueDate() const;
  /// \pre value type is Type::Time
  const ConstTime ValueTime() const;
  /// \pre value type is Type::LocalTime
  const ConstLocalTime ValueLocalTime() const;
  /// \pre value type is Type::DateTime
  const ConstDateTime ValueDateTime() const;
  /// \pre value type is Type::DateTimeZoneId
  const ConstDateTimeZoneId ValueDateTimeZoneId() const;
  /// \pre value type is Type::LocalDateTime
  const ConstLocalDateTime ValueLocalDateTime() const;
  /// \pre value type is Type::Duration
  const ConstDuration ValueDuration() const;
  /// \pre value type is Type::Point2d
  const ConstPoint2d ValuePoint2d() const;
  /// \pre value type is Type::Point3d
  const ConstPoint3d ValuePoint3d() const;

  /// \exception std::runtime_error the value type is unknown
  Value::Type type() const;

  /// \exception std::runtime_error the value type is unknown
  bool operator==(const ConstValue &other) const;
  /// \exception std::runtime_error the value type is unknown
  bool operator==(const Value &other) const;
  /// \exception std::runtime_error the value type is unknown
  bool operator!=(const ConstValue &other) const { return !(*this == other); }
  /// \exception std::runtime_error the value type is unknown
  bool operator!=(const Value &other) const { return !(*this == other); }

  const mg_value *ptr() const { return const_ptr_; }

 private:
  const mg_value *const_ptr_;
};

#undef CREATE_ITERATOR

namespace detail {
inline std::string_view ConvertString(const mg_string *str) {
  return std::string_view(mg_string_data(str), mg_string_size(str));
}

inline Value::Type ConvertType(mg_value_type type) {
  switch (type) {
    case MG_VALUE_TYPE_NULL:
      return Value::Type::Null;
    case MG_VALUE_TYPE_BOOL:
      return Value::Type::Bool;
    case MG_VALUE_TYPE_INTEGER:
      return Value::Type::Int;
    case MG_VALUE_TYPE_FLOAT:
      return Value::Type::Double;
    case MG_VALUE_TYPE_STRING:
      return Value::Type::String;
    case MG_VALUE_TYPE_LIST:
      return Value::Type::List;
    case MG_VALUE_TYPE_MAP:
      return Value::Type::Map;
    case MG_VALUE_TYPE_NODE:
      return Value::Type::Node;
    case MG_VALUE_TYPE_RELATIONSHIP:
      return Value::Type::Relationship;
    case MG_VALUE_TYPE_UNBOUND_RELATIONSHIP:
      return Value::Type::UnboundRelationship;
    case MG_VALUE_TYPE_PATH:
      return Value::Type::Path;
    case MG_VALUE_TYPE_DATE:
      return Value::Type::Date;
    case MG_VALUE_TYPE_TIME:
      return Value::Type::Time;
    case MG_VALUE_TYPE_LOCAL_TIME:
      return Value::Type::LocalTime;
    case MG_VALUE_TYPE_DATE_TIME:
      return Value::Type::DateTime;
    case MG_VALUE_TYPE_DATE_TIME_ZONE_ID:
      return Value::Type::DateTimeZoneId;
    case MG_VALUE_TYPE_LOCAL_DATE_TIME:
      return Value::Type::LocalDateTime;
    case MG_VALUE_TYPE_DURATION:
      return Value::Type::Duration;
    case MG_VALUE_TYPE_POINT_2D:
      return Value::Type::Point2d;
    case MG_VALUE_TYPE_POINT_3D:
      return Value::Type::Point3d;
    case MG_VALUE_TYPE_UNKNOWN:
      throw std::runtime_error("Unknown value type!");
  }
  std::abort();
}

inline bool AreValuesEqual(const mg_value *value1, const mg_value *value2);

inline bool AreListsEqual(const mg_list *list1, const mg_list *list2) {
  if (list1 == list2) {
    return true;
  }
  if (mg_list_size(list1) != mg_list_size(list2)) {
    return false;
  }
  const size_t len = mg_list_size(list1);
  for (size_t i = 0; i < len; ++i) {
    if (!detail::AreValuesEqual(mg_list_at(list1, i), mg_list_at(list2, i))) {
      return false;
    }
  }
  return true;
}

inline bool AreMapsEqual(const mg_map *map1, const mg_map *map2) {
  if (map1 == map2) {
    return true;
  }
  if (mg_map_size(map1) != mg_map_size(map2)) {
    return false;
  }
  const size_t len = mg_map_size(map1);
  for (size_t i = 0; i < len; ++i) {
    const mg_string *key = mg_map_key_at(map1, i);
    const mg_value *value1 = mg_map_value_at(map1, i);
    const mg_value *value2 =
        mg_map_at2(map2, mg_string_size(key), mg_string_data(key));
    if (value2 == nullptr) {
      return false;
    }
    if (!detail::AreValuesEqual(value1, value2)) {
      return false;
    }
  }
  return true;
}

inline bool AreNodesEqual(const mg_node *node1, const mg_node *node2) {
  if (node1 == node2) {
    return true;
  }
  if (mg_node_id(node1) != mg_node_id(node2)) {
    return false;
  }
  if (mg_node_label_count(node1) != mg_node_label_count(node2)) {
    return false;
  }
  std::set<std::string_view> labels1;
  std::set<std::string_view> labels2;
  const size_t label_count = mg_node_label_count(node1);
  for (size_t i = 0; i < label_count; ++i) {
    labels1.insert(detail::ConvertString(mg_node_label_at(node1, i)));
    labels2.insert(detail::ConvertString(mg_node_label_at(node2, i)));
  }
  if (labels1 != labels2) {
    return false;
  }
  return detail::AreMapsEqual(mg_node_properties(node1),
                              mg_node_properties(node2));
}

inline bool AreRelationshipsEqual(const mg_relationship *rel1,
                                  const mg_relationship *rel2) {
  if (rel1 == rel2) {
    return true;
  }
  if (mg_relationship_id(rel1) != mg_relationship_id(rel2)) {
    return false;
  }
  if (mg_relationship_start_id(rel1) != mg_relationship_start_id(rel2)) {
    return false;
  }
  if (mg_relationship_end_id(rel1) != mg_relationship_end_id(rel2)) {
    return false;
  }
  if (detail::ConvertString(mg_relationship_type(rel1)) !=
      detail::ConvertString(mg_relationship_type(rel2))) {
    return false;
  }
  return detail::AreMapsEqual(mg_relationship_properties(rel1),
                              mg_relationship_properties(rel2));
}

inline bool AreUnboundRelationshipsEqual(const mg_unbound_relationship *rel1,
                                         const mg_unbound_relationship *rel2) {
  if (rel1 == rel2) {
    return true;
  }
  if (mg_unbound_relationship_id(rel1) != mg_unbound_relationship_id(rel2)) {
    return false;
  }
  if (detail::ConvertString(mg_unbound_relationship_type(rel1)) !=
      detail::ConvertString(mg_unbound_relationship_type(rel2))) {
    return false;
  }
  return detail::AreMapsEqual(mg_unbound_relationship_properties(rel1),
                              mg_unbound_relationship_properties(rel2));
}

inline bool ArePathsEqual(const mg_path *path1, const mg_path *path2) {
  if (path1 == path2) {
    return true;
  }
  if (mg_path_length(path1) != mg_path_length(path2)) {
    return false;
  }
  const size_t len = mg_path_length(path1);
  for (size_t i = 0; i < len; ++i) {
    if (!detail::AreNodesEqual(mg_path_node_at(path1, i),
                               mg_path_node_at(path2, i))) {
      return false;
    }
    if (!detail::AreUnboundRelationshipsEqual(
            mg_path_relationship_at(path1, i),
            mg_path_relationship_at(path2, i))) {
      return false;
    }
    if (mg_path_relationship_reversed_at(path1, i) !=
        mg_path_relationship_reversed_at(path2, i)) {
      return false;
    }
  }
  return detail::AreNodesEqual(mg_path_node_at(path1, len),
                               mg_path_node_at(path2, len));
}

inline bool AreDatesEqual(const mg_date *date1, const mg_date *date2) {
  return mg_date_days(date1) == mg_date_days(date2);
}

inline bool AreTimesEqual(const mg_time *time1, const mg_time *time2) {
  return mg_time_nanoseconds(time1) == mg_time_nanoseconds(time2) &&
         mg_time_tz_offset_seconds(time1) == mg_time_tz_offset_seconds(time2);
}

inline bool AreLocalTimesEqual(const mg_local_time *local_time1,
                               const mg_local_time *local_time2) {
  return mg_local_time_nanoseconds(local_time1) ==
         mg_local_time_nanoseconds(local_time2);
}

inline bool AreDateTimesEqual(const mg_date_time *date_time1,
                              const mg_date_time *date_time2) {
  return mg_date_time_seconds(date_time1) == mg_date_time_seconds(date_time2) &&
         mg_date_time_nanoseconds(date_time1) ==
             mg_date_time_nanoseconds(date_time2) &&
         mg_date_time_tz_offset_minutes(date_time1) ==
             mg_date_time_tz_offset_minutes(date_time2);
}

inline bool AreDateTimeZoneIdsEqual(
    const mg_date_time_zone_id *date_time_zone_id1,
    const mg_date_time_zone_id *date_time_zone_id2) {
  return mg_date_time_zone_id_seconds(date_time_zone_id1) ==
             mg_date_time_zone_id_nanoseconds(date_time_zone_id2) &&
         mg_date_time_zone_id_nanoseconds(date_time_zone_id1) ==
             mg_date_time_zone_id_nanoseconds(date_time_zone_id2) &&
         mg_date_time_zone_id_tz_id(date_time_zone_id1) ==
             mg_date_time_zone_id_tz_id(date_time_zone_id2);
}

inline bool AreLocalDateTimesEqual(const mg_local_date_time *local_date_time1,
                                   const mg_local_date_time *local_date_time2) {
  return mg_local_date_time_seconds(local_date_time1) ==
             mg_local_date_time_nanoseconds(local_date_time2) &&
         mg_local_date_time_nanoseconds(local_date_time1) ==
             mg_local_date_time_nanoseconds(local_date_time2);
}

inline bool AreDurationsEqual(const mg_duration *duration1,
                              const mg_duration *duration2) {
  return mg_duration_months(duration1) == mg_duration_months(duration2) &&
         mg_duration_days(duration1) == mg_duration_days(duration2) &&
         mg_duration_seconds(duration1) == mg_duration_seconds(duration2) &&
         mg_duration_nanoseconds(duration1) ==
             mg_duration_nanoseconds(duration2);
}

inline bool ArePoint2dsEqual(const mg_point_2d *point_2d1,
                             const mg_point_2d *point_2d2) {
  return mg_point_2d_srid(point_2d1) == mg_point_2d_srid(point_2d2) &&
         mg_point_2d_x(point_2d1) == mg_point_2d_x(point_2d2) &&
         mg_point_2d_y(point_2d1) == mg_point_2d_y(point_2d2);
}

inline bool ArePoint3dsEqual(const mg_point_3d *point_3d1,
                             const mg_point_3d *point_3d2) {
  return mg_point_3d_srid(point_3d1) == mg_point_3d_srid(point_3d2) &&
         mg_point_3d_x(point_3d1) == mg_point_3d_x(point_3d2) &&
         mg_point_3d_y(point_3d1) == mg_point_3d_y(point_3d2) &&
         mg_point_3d_z(point_3d1) == mg_point_3d_z(point_3d2);
}

inline bool AreValuesEqual(const mg_value *value1, const mg_value *value2) {
  if (value1 == value2) {
    return true;
  }
  if (mg_value_get_type(value1) != mg_value_get_type(value2)) {
    return false;
  }
  switch (mg_value_get_type(value1)) {
    case MG_VALUE_TYPE_NULL:
      return true;
    case MG_VALUE_TYPE_BOOL:
      return mg_value_bool(value1) == mg_value_bool(value2);
    case MG_VALUE_TYPE_INTEGER:
      return mg_value_integer(value1) == mg_value_integer(value2);
    case MG_VALUE_TYPE_FLOAT:
      return mg_value_float(value1) == mg_value_float(value2);
    case MG_VALUE_TYPE_STRING:
      return detail::ConvertString(mg_value_string(value1)) ==
             detail::ConvertString(mg_value_string(value2));
    case MG_VALUE_TYPE_LIST:
      return detail::AreListsEqual(mg_value_list(value1),
                                   mg_value_list(value2));
    case MG_VALUE_TYPE_MAP:
      return detail::AreMapsEqual(mg_value_map(value1), mg_value_map(value2));
    case MG_VALUE_TYPE_NODE:
      return detail::AreNodesEqual(mg_value_node(value1),
                                   mg_value_node(value2));
    case MG_VALUE_TYPE_RELATIONSHIP:
      return detail::AreRelationshipsEqual(mg_value_relationship(value1),
                                           mg_value_relationship(value2));
    case MG_VALUE_TYPE_UNBOUND_RELATIONSHIP:
      return detail::AreUnboundRelationshipsEqual(
          mg_value_unbound_relationship(value1),
          mg_value_unbound_relationship(value2));
    case MG_VALUE_TYPE_PATH:
      return detail::ArePathsEqual(mg_value_path(value1),
                                   mg_value_path(value2));
    case MG_VALUE_TYPE_DATE:
      return detail::AreDatesEqual(mg_value_date(value1),
                                   mg_value_date(value2));
    case MG_VALUE_TYPE_TIME:
      return detail::AreTimesEqual(mg_value_time(value1),
                                   mg_value_time(value2));
    case MG_VALUE_TYPE_LOCAL_TIME:
      return detail::AreLocalTimesEqual(mg_value_local_time(value1),
                                        mg_value_local_time(value2));
    case MG_VALUE_TYPE_DATE_TIME:
      return detail::AreDateTimesEqual(mg_value_date_time(value1),
                                       mg_value_date_time(value2));
    case MG_VALUE_TYPE_DATE_TIME_ZONE_ID:
      return detail::AreDateTimeZoneIdsEqual(
          mg_value_date_time_zone_id(value1),
          mg_value_date_time_zone_id(value2));
    case MG_VALUE_TYPE_LOCAL_DATE_TIME:
      return detail::AreLocalDateTimesEqual(mg_value_local_date_time(value1),
                                            mg_value_local_date_time(value2));
    case MG_VALUE_TYPE_DURATION:
      return detail::AreDurationsEqual(mg_value_duration(value1),
                                       mg_value_duration(value2));
    case MG_VALUE_TYPE_POINT_2D:
      return detail::ArePoint2dsEqual(mg_value_point_2d(value1),
                                      mg_value_point_2d(value2));
    case MG_VALUE_TYPE_POINT_3D:
      return detail::ArePoint3dsEqual(mg_value_point_3d(value1),
                                      mg_value_point_3d(value2));
    case MG_VALUE_TYPE_UNKNOWN:
      throw std::runtime_error("Comparing values of unknown types!");
  }
  std::abort();
}
}  // namespace detail

////////////////////////////////////////////////////////////////////////////////
// List:

inline ConstValue List::Iterator::operator*() const {
  return (*iterable_)[index_];
}

inline List::List(const List &other) : ptr_(mg_list_copy(other.ptr_)) {}

inline List::List(List &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

inline List::~List() {
  if (ptr_ != nullptr) {
    mg_list_destroy(ptr_);
  }
}

inline List::List(const ConstList &list) : ptr_(mg_list_copy(list.ptr())) {}

inline List::List(const std::vector<mg::Value> &values) : List(values.size()) {
  for (const auto &value : values) {
    Append(value);
  }
}

inline List::List(std::vector<mg::Value> &&values) : List(values.size()) {
  for (auto &value : values) {
    Append(std::move(value));
  }
}

inline List::List(std::initializer_list<Value> values) : List(values.size()) {
  for (const auto &value : values) {
    Append(value);
  }
}

inline const ConstValue List::operator[](size_t index) const {
  return ConstValue(mg_list_at(ptr_, index));
}

inline bool List::Append(const Value &value) {
  return mg_list_append(ptr_, mg_value_copy(value.ptr())) == 0;
}

inline bool List::Append(const ConstValue &value) {
  return mg_list_append(ptr_, mg_value_copy(value.ptr())) == 0;
}

inline bool List::Append(Value &&value) {
  bool result = mg_list_append(ptr_, value.ptr_) == 0;
  value.ptr_ = nullptr;
  return result;
}

inline const ConstList List::AsConstList() const { return ConstList(ptr_); }

inline bool List::operator==(const List &other) const {
  return detail::AreListsEqual(ptr_, other.ptr_);
}

inline bool List::operator==(const ConstList &other) const {
  return detail::AreListsEqual(ptr_, other.ptr());
}

inline ConstValue ConstList::Iterator::operator*() const {
  return (*iterable_)[index_];
}

inline const ConstValue ConstList::operator[](size_t index) const {
  return ConstValue(mg_list_at(const_ptr_, index));
}

inline bool ConstList::operator==(const ConstList &other) const {
  return detail::AreListsEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstList::operator==(const List &other) const {
  return detail::AreListsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Map:

inline std::pair<std::string_view, ConstValue> Map::Iterator::operator*()
    const {
  return std::make_pair(
      detail::ConvertString(mg_map_key_at(iterable_->ptr(), index_)),
      ConstValue(mg_map_value_at(iterable_->ptr(), index_)));
}

inline Map::Map(const Map &other) : Map(mg_map_copy(other.ptr_)) {}

inline Map::Map(Map &&other) : Map(other.ptr_) { other.ptr_ = nullptr; }

inline Map::Map(const ConstMap &map) : ptr_(mg_map_copy(map.ptr())) {}

inline Map::~Map() {
  if (ptr_ != nullptr) {
    mg_map_destroy(ptr_);
  }
}

inline Map::Map(std::initializer_list<std::pair<std::string, Value>> list)
    : Map(list.size()) {
  for (const auto &[key, value] : list) {
    Insert(key, value.AsConstValue());
  }
}

inline ConstValue Map::operator[](const std::string_view key) const {
  return ConstValue(mg_map_at2(ptr_, key.size(), key.data()));
}

inline Map::Iterator Map::find(const std::string_view key) const {
  for (size_t i = 0; i < size(); ++i) {
    if (key == detail::ConvertString(mg_map_key_at(ptr_, i))) {
      return Iterator(this, i);
    }
  }
  return end();
}

inline bool Map::Insert(const std::string_view key, const Value &value) {
  return mg_map_insert2(ptr_, mg_string_make2(key.size(), key.data()),
                        mg_value_copy(value.ptr())) == 0;
}

inline bool Map::Insert(const std::string_view key, const ConstValue &value) {
  return mg_map_insert2(ptr_, mg_string_make2(key.size(), key.data()),
                        mg_value_copy(value.ptr())) == 0;
}

inline bool Map::Insert(const std::string_view key, Value &&value) {
  bool result = mg_map_insert2(ptr_, mg_string_make2(key.size(), key.data()),
                               value.ptr_) == 0;
  value.ptr_ = nullptr;
  return result;
}

inline bool Map::InsertUnsafe(const std::string_view key, const Value &value) {
  return mg_map_insert_unsafe2(ptr_, mg_string_make2(key.size(), key.data()),
                               mg_value_copy(value.ptr())) == 0;
}

inline bool Map::InsertUnsafe(const std::string_view key,
                              const ConstValue &value) {
  return mg_map_insert_unsafe2(ptr_, mg_string_make2(key.size(), key.data()),
                               mg_value_copy(value.ptr())) == 0;
}

inline bool Map::InsertUnsafe(const std::string_view key, Value &&value) {
  bool result =
      mg_map_insert_unsafe2(ptr_, mg_string_make2(key.size(), key.data()),
                            value.ptr_) == 0;
  value.ptr_ = nullptr;
  return result;
}

inline const ConstMap Map::AsConstMap() const { return ConstMap(ptr_); }

inline bool Map::operator==(const Map &other) const {
  return detail::AreMapsEqual(ptr_, other.ptr_);
}

inline bool Map::operator==(const ConstMap &other) const {
  return detail::AreMapsEqual(ptr_, other.ptr());
}

inline std::pair<std::string_view, ConstValue> ConstMap::Iterator::operator*()
    const {
  return std::make_pair(
      detail::ConvertString(mg_map_key_at(iterable_->ptr(), index_)),
      ConstValue(mg_map_value_at(iterable_->ptr(), index_)));
}

inline ConstValue ConstMap::operator[](const std::string_view key) const {
  return ConstValue(mg_map_at2(const_ptr_, key.size(), key.data()));
}

inline ConstMap::Iterator ConstMap::find(const std::string_view key) const {
  for (size_t i = 0; i < size(); ++i) {
    if (key == detail::ConvertString(mg_map_key_at(const_ptr_, i))) {
      return Iterator(this, i);
    }
  }
  return end();
}

inline bool ConstMap::operator==(const ConstMap &other) const {
  return detail::AreMapsEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstMap::operator==(const Map &other) const {
  return detail::AreMapsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Node:

inline std::string_view Node::Labels::Iterator::operator*() const {
  return (*iterable_)[index_];
}

inline std::string_view Node::Labels::operator[](size_t index) const {
  return detail::ConvertString(mg_node_label_at(node_, index));
}

inline Node::Node(const Node &other) : Node(mg_node_copy(other.ptr_)) {}

inline Node::Node(Node &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

inline Node::~Node() {
  if (ptr_ != nullptr) {
    mg_node_destroy(ptr_);
  }
}

inline Node::Node(const ConstNode &node) : ptr_(mg_node_copy(node.ptr())) {}

inline bool Node::operator==(const Node &other) const {
  return detail::AreNodesEqual(ptr_, other.ptr_);
}

inline bool Node::operator==(const ConstNode &other) const {
  return detail::AreNodesEqual(ptr_, other.ptr());
}

inline ConstNode Node::AsConstNode() const { return ConstNode(ptr_); }

inline bool ConstNode::operator==(const ConstNode &other) const {
  return detail::AreNodesEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstNode::operator==(const Node &other) const {
  return detail::AreNodesEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Relationship:

inline Relationship::Relationship(const Relationship &other)
    : Relationship(mg_relationship_copy(other.ptr_)) {}

inline Relationship::Relationship(Relationship &&other)
    : Relationship(other.ptr_) {
  other.ptr_ = nullptr;
}

inline Relationship::~Relationship() {
  if (ptr_ != nullptr) {
    mg_relationship_destroy(ptr_);
  }
}

inline Relationship::Relationship(const ConstRelationship &rel)
    : ptr_(mg_relationship_copy(rel.ptr())) {}

inline std::string_view Relationship::type() const {
  return detail::ConvertString(mg_relationship_type(ptr_));
}

inline ConstRelationship Relationship::AsConstRelationship() const {
  return ConstRelationship(ptr_);
}

inline bool Relationship::operator==(const Relationship &other) const {
  return detail::AreRelationshipsEqual(ptr_, other.ptr_);
}

inline bool Relationship::operator==(const ConstRelationship &other) const {
  return detail::AreRelationshipsEqual(ptr_, other.ptr());
}

inline std::string_view ConstRelationship::type() const {
  return detail::ConvertString(mg_relationship_type(const_ptr_));
}

inline bool ConstRelationship::operator==(
    const ConstRelationship &other) const {
  return detail::AreRelationshipsEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstRelationship::operator==(const Relationship &other) const {
  return detail::AreRelationshipsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// UnboundRelationship:

inline UnboundRelationship::UnboundRelationship(
    const UnboundRelationship &other)
    : ptr_(mg_unbound_relationship_copy(other.ptr_)) {}

inline UnboundRelationship::UnboundRelationship(UnboundRelationship &&other)
    : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

inline UnboundRelationship::~UnboundRelationship() {
  if (ptr_ != nullptr) {
    mg_unbound_relationship_destroy(ptr_);
  }
}

inline UnboundRelationship::UnboundRelationship(
    const ConstUnboundRelationship &rel)
    : ptr_(mg_unbound_relationship_copy(rel.ptr())) {}

inline std::string_view UnboundRelationship::type() const {
  return detail::ConvertString(mg_unbound_relationship_type(ptr_));
}

inline ConstUnboundRelationship
UnboundRelationship::AsConstUnboundRelationship() const {
  return ConstUnboundRelationship(ptr_);
}

inline bool UnboundRelationship::operator==(
    const UnboundRelationship &other) const {
  return detail::AreUnboundRelationshipsEqual(ptr_, other.ptr_);
}

inline bool UnboundRelationship::operator==(
    const ConstUnboundRelationship &other) const {
  return detail::AreUnboundRelationshipsEqual(ptr_, other.ptr());
}

inline std::string_view ConstUnboundRelationship::type() const {
  return detail::ConvertString(mg_unbound_relationship_type(const_ptr_));
}

inline bool ConstUnboundRelationship::operator==(
    const ConstUnboundRelationship &other) const {
  return detail::AreUnboundRelationshipsEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstUnboundRelationship::operator==(
    const UnboundRelationship &other) const {
  return detail::AreUnboundRelationshipsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Path:

inline Path::Path(const Path &other) : ptr_(mg_path_copy(other.ptr_)) {}

inline Path::Path(Path &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

inline Path::~Path() {
  if (ptr_ != nullptr) {
    mg_path_destroy(ptr_);
  }
}

inline Path::Path(const ConstPath &path) : ptr_(mg_path_copy(path.ptr())) {}

inline ConstNode Path::GetNodeAt(size_t index) const {
  auto vertex_ptr = mg_path_node_at(ptr_, index);
  if (vertex_ptr == nullptr) {
    std::abort();
  }
  return ConstNode(vertex_ptr);
}

inline ConstUnboundRelationship Path::GetRelationshipAt(size_t index) const {
  auto edge_ptr = mg_path_relationship_at(ptr_, index);
  if (edge_ptr == nullptr) {
    std::abort();
  }
  return ConstUnboundRelationship(edge_ptr);
}

inline bool Path::IsReversedRelationshipAt(size_t index) const {
  auto is_reversed = mg_path_relationship_reversed_at(ptr_, index);
  if (is_reversed == -1) {
    std::abort();
  }
  return is_reversed == 1;
}

inline ConstPath Path::AsConstPath() const { return ConstPath(ptr_); }

inline bool Path::operator==(const Path &other) const {
  return detail::ArePathsEqual(ptr_, other.ptr_);
}

inline bool Path::operator==(const ConstPath &other) const {
  return detail::ArePathsEqual(ptr_, other.ptr());
}

inline ConstNode ConstPath::GetNodeAt(size_t index) const {
  auto vertex_ptr = mg_path_node_at(const_ptr_, index);
  if (vertex_ptr == nullptr) {
    std::abort();
  }
  return ConstNode(vertex_ptr);
}

inline ConstUnboundRelationship ConstPath::GetRelationshipAt(
    size_t index) const {
  auto edge_ptr = mg_path_relationship_at(const_ptr_, index);
  if (edge_ptr == nullptr) {
    std::abort();
  }
  return ConstUnboundRelationship(edge_ptr);
}

inline bool ConstPath::IsReversedRelationshipAt(size_t index) const {
  auto is_reversed = mg_path_relationship_reversed_at(const_ptr_, index);
  if (is_reversed == -1) {
    std::abort();
  }
  return is_reversed == 1;
}

inline bool ConstPath::operator==(const ConstPath &other) const {
  return detail::ArePathsEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstPath::operator==(const Path &other) const {
  return detail::ArePathsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Date:

inline Date::Date(const Date &other) : ptr_(mg_date_copy(other.ptr_)) {}

inline Date::Date(Date &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

inline Date &Date::operator=(const Date &other) {
  ptr_ = mg_date_copy(other.ptr_);
  return *this;
}

inline Date &Date::operator=(Date &&other) {
  ptr_ = other.ptr_;
  other.ptr_ = nullptr;
  return *this;
}

inline Date::~Date() {
  if (ptr_ != nullptr) {
    mg_date_destroy(ptr_);
  }
}

inline Date::Date(const ConstDate &date) : ptr_(mg_date_copy(date.ptr())) {}

inline ConstDate Date::AsConstDate() const { return ConstDate(ptr_); }

inline bool Date::operator==(const Date &other) const {
  return detail::AreDatesEqual(ptr_, other.ptr_);
}

inline bool Date::operator==(const ConstDate &other) const {
  return detail::AreDatesEqual(ptr_, other.ptr());
}

inline bool ConstDate::operator==(const ConstDate &other) const {
  return detail::AreDatesEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstDate::operator==(const Date &other) const {
  return detail::AreDatesEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Time:

inline Time::Time(const Time &other) : ptr_(mg_time_copy(other.ptr_)) {}

inline Time::Time(Time &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

inline Time &Time::operator=(const Time &other) {
  ptr_ = mg_time_copy(other.ptr_);
  return *this;
}

inline Time &Time::operator=(Time &&other) {
  ptr_ = other.ptr_;
  other.ptr_ = nullptr;
  return *this;
}

inline Time::~Time() {
  if (ptr_ != nullptr) {
    mg_time_destroy(ptr_);
  }
}

inline Time::Time(const ConstTime &time) : ptr_(mg_time_copy(time.ptr())) {}

inline ConstTime Time::AsConstTime() const { return ConstTime(ptr_); }

inline bool Time::operator==(const Time &other) const {
  return detail::AreTimesEqual(ptr_, other.ptr_);
}

inline bool Time::operator==(const ConstTime &other) const {
  return detail::AreTimesEqual(ptr_, other.ptr());
}

inline bool ConstTime::operator==(const ConstTime &other) const {
  return detail::AreTimesEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstTime::operator==(const Time &other) const {
  return detail::AreTimesEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// LocalTime:

inline LocalTime::LocalTime(const LocalTime &other)
    : ptr_(mg_local_time_copy(other.ptr_)) {}

inline LocalTime::LocalTime(LocalTime &&other) : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

inline LocalTime &LocalTime::operator=(const LocalTime &other) {
  ptr_ = mg_local_time_copy(other.ptr_);
  return *this;
}

inline LocalTime &LocalTime::operator=(LocalTime &&other) {
  ptr_ = other.ptr_;
  other.ptr_ = nullptr;
  return *this;
}

inline LocalTime::~LocalTime() {
  if (ptr_ != nullptr) {
    mg_local_time_destroy(ptr_);
  }
}

inline LocalTime::LocalTime(const ConstLocalTime &local_time)
    : ptr_(mg_local_time_copy(local_time.ptr())) {}

inline ConstLocalTime LocalTime::AsConstLocalTime() const {
  return ConstLocalTime(ptr_);
}

inline bool LocalTime::operator==(const LocalTime &other) const {
  return detail::AreLocalTimesEqual(ptr_, other.ptr_);
}

inline bool LocalTime::operator==(const ConstLocalTime &other) const {
  return detail::AreLocalTimesEqual(ptr_, other.ptr());
}

inline bool ConstLocalTime::operator==(const ConstLocalTime &other) const {
  return detail::AreLocalTimesEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstLocalTime::operator==(const LocalTime &other) const {
  return detail::AreLocalTimesEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// DateTime:

inline DateTime::DateTime(const DateTime &other)
    : ptr_(mg_date_time_copy(other.ptr_)) {}

inline DateTime::DateTime(DateTime &&other) : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

inline DateTime &DateTime::operator=(const DateTime &other) {
  ptr_ = mg_date_time_copy(other.ptr_);
  return *this;
}

inline DateTime &DateTime::operator=(DateTime &&other) {
  ptr_ = other.ptr_;
  other.ptr_ = nullptr;
  return *this;
}

inline DateTime::~DateTime() {
  if (ptr_ != nullptr) {
    mg_date_time_destroy(ptr_);
  }
}

inline DateTime::DateTime(const ConstDateTime &date_time)
    : ptr_(mg_date_time_copy(date_time.ptr())) {}

inline ConstDateTime DateTime::AsConstDateTime() const {
  return ConstDateTime(ptr_);
}

inline bool DateTime::operator==(const DateTime &other) const {
  return detail::AreDateTimesEqual(ptr_, other.ptr_);
}

inline bool DateTime::operator==(const ConstDateTime &other) const {
  return detail::AreDateTimesEqual(ptr_, other.ptr());
}

inline bool ConstDateTime::operator==(const ConstDateTime &other) const {
  return detail::AreDateTimesEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstDateTime::operator==(const DateTime &other) const {
  return detail::AreDateTimesEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// DateTimeZoneId:

inline DateTimeZoneId::DateTimeZoneId(const DateTimeZoneId &other)
    : ptr_(mg_date_time_zone_id_copy(other.ptr_)) {}

inline DateTimeZoneId::DateTimeZoneId(DateTimeZoneId &&other)
    : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

inline DateTimeZoneId &DateTimeZoneId::operator=(const DateTimeZoneId &other) {
  ptr_ = mg_date_time_zone_id_copy(other.ptr_);
  return *this;
}

inline DateTimeZoneId &DateTimeZoneId::operator=(DateTimeZoneId &&other) {
  ptr_ = other.ptr_;
  other.ptr_ = nullptr;
  return *this;
}

inline DateTimeZoneId::~DateTimeZoneId() {
  if (ptr_ != nullptr) {
    mg_date_time_zone_id_destroy(ptr_);
  }
}

inline DateTimeZoneId::DateTimeZoneId(
    const ConstDateTimeZoneId &date_time_zone_id)
    : ptr_(mg_date_time_zone_id_copy(date_time_zone_id.ptr())) {}

inline ConstDateTimeZoneId DateTimeZoneId::AsConstDateTimeZoneId() const {
  return ConstDateTimeZoneId(ptr_);
}

inline bool DateTimeZoneId::operator==(const DateTimeZoneId &other) const {
  return detail::AreDateTimeZoneIdsEqual(ptr_, other.ptr_);
}

inline bool DateTimeZoneId::operator==(const ConstDateTimeZoneId &other) const {
  return detail::AreDateTimeZoneIdsEqual(ptr_, other.ptr());
}

inline bool ConstDateTimeZoneId::operator==(
    const ConstDateTimeZoneId &other) const {
  return detail::AreDateTimeZoneIdsEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstDateTimeZoneId::operator==(const DateTimeZoneId &other) const {
  return detail::AreDateTimeZoneIdsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// LocalDateTime:

inline LocalDateTime::LocalDateTime(const LocalDateTime &other)
    : ptr_(mg_local_date_time_copy(other.ptr_)) {}

inline LocalDateTime::LocalDateTime(LocalDateTime &&other) : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

inline LocalDateTime &LocalDateTime::operator=(const LocalDateTime &other) {
  ptr_ = mg_local_date_time_copy(other.ptr_);
  return *this;
}

inline LocalDateTime &LocalDateTime::operator=(LocalDateTime &&other) {
  ptr_ = other.ptr_;
  other.ptr_ = nullptr;
  return *this;
}

inline LocalDateTime::~LocalDateTime() {
  if (ptr_ != nullptr) {
    mg_local_date_time_destroy(ptr_);
  }
}

inline LocalDateTime::LocalDateTime(const ConstLocalDateTime &local_date_time)
    : ptr_(mg_local_date_time_copy(local_date_time.ptr())) {}

inline ConstLocalDateTime LocalDateTime::AsConstLocalDateTime() const {
  return ConstLocalDateTime(ptr_);
}

inline bool LocalDateTime::operator==(const LocalDateTime &other) const {
  return detail::AreLocalDateTimesEqual(ptr_, other.ptr_);
}

inline bool LocalDateTime::operator==(const ConstLocalDateTime &other) const {
  return detail::AreLocalDateTimesEqual(ptr_, other.ptr());
}

inline bool ConstLocalDateTime::operator==(
    const ConstLocalDateTime &other) const {
  return detail::AreLocalDateTimesEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstLocalDateTime::operator==(const LocalDateTime &other) const {
  return detail::AreLocalDateTimesEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Duration:

inline Duration::Duration(const Duration &other)
    : ptr_(mg_duration_copy(other.ptr_)) {}

inline Duration::Duration(Duration &&other) : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

inline Duration &Duration::operator=(const Duration &other) {
  ptr_ = mg_duration_copy(other.ptr_);
  return *this;
}

inline Duration &Duration::operator=(Duration &&other) {
  ptr_ = other.ptr_;
  other.ptr_ = nullptr;
  return *this;
}

inline Duration::~Duration() {
  if (ptr_ != nullptr) {
    mg_duration_destroy(ptr_);
  }
}

inline Duration::Duration(const ConstDuration &duration)
    : ptr_(mg_duration_copy(duration.ptr())) {}

inline ConstDuration Duration::AsConstDuration() const {
  return ConstDuration(ptr_);
}

inline bool Duration::operator==(const Duration &other) const {
  return detail::AreDurationsEqual(ptr_, other.ptr_);
}

inline bool Duration::operator==(const ConstDuration &other) const {
  return detail::AreDurationsEqual(ptr_, other.ptr());
}

inline bool ConstDuration::operator==(const ConstDuration &other) const {
  return detail::AreDurationsEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstDuration::operator==(const Duration &other) const {
  return detail::AreDurationsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Point2d:

inline Point2d::Point2d(const Point2d &other)
    : ptr_(mg_point_2d_copy(other.ptr_)) {}

inline Point2d::Point2d(Point2d &&other) : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

inline Point2d &Point2d::operator=(const Point2d &other) {
  ptr_ = mg_point_2d_copy(other.ptr_);
  return *this;
}

inline Point2d &Point2d::operator=(Point2d &&other) {
  ptr_ = other.ptr_;
  other.ptr_ = nullptr;
  return *this;
}

inline Point2d::~Point2d() {
  if (ptr_ != nullptr) {
    mg_point_2d_destroy(ptr_);
  }
}

inline Point2d::Point2d(const ConstPoint2d &point_2d)
    : ptr_(mg_point_2d_copy(point_2d.ptr())) {}

inline ConstPoint2d Point2d::AsConstPoint2d() const {
  return ConstPoint2d(ptr_);
}

inline bool Point2d::operator==(const Point2d &other) const {
  return detail::ArePoint2dsEqual(ptr_, other.ptr_);
}

inline bool Point2d::operator==(const ConstPoint2d &other) const {
  return detail::ArePoint2dsEqual(ptr_, other.ptr());
}

inline bool ConstPoint2d::operator==(const ConstPoint2d &other) const {
  return detail::ArePoint2dsEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstPoint2d::operator==(const Point2d &other) const {
  return detail::ArePoint2dsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Point3d:

inline Point3d::Point3d(const Point3d &other)
    : ptr_(mg_point_3d_copy(other.ptr_)) {}

inline Point3d::Point3d(Point3d &&other) : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

inline Point3d &Point3d::operator=(const Point3d &other) {
  ptr_ = mg_point_3d_copy(other.ptr_);
  return *this;
}

inline Point3d &Point3d::operator=(Point3d &&other) {
  ptr_ = other.ptr_;
  other.ptr_ = nullptr;
  return *this;
}

inline Point3d::~Point3d() {
  if (ptr_ != nullptr) {
    mg_point_3d_destroy(ptr_);
  }
}

inline Point3d::Point3d(const ConstPoint3d &point_3d)
    : ptr_(mg_point_3d_copy(point_3d.ptr())) {}

inline ConstPoint3d Point3d::AsConstPoint3d() const {
  return ConstPoint3d(ptr_);
}

inline bool Point3d::operator==(const Point3d &other) const {
  return detail::ArePoint3dsEqual(ptr_, other.ptr_);
}

inline bool Point3d::operator==(const ConstPoint3d &other) const {
  return detail::ArePoint3dsEqual(ptr_, other.ptr());
}

inline bool ConstPoint3d::operator==(const ConstPoint3d &other) const {
  return detail::ArePoint3dsEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstPoint3d::operator==(const Point3d &other) const {
  return detail::ArePoint3dsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Value:

inline Value::Value(const Value &other) : Value(mg_value_copy(other.ptr_)) {}

inline Value::Value(Value &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

inline Value::~Value() {
  if (ptr_ != nullptr) {
    mg_value_destroy(ptr_);
  }
}

inline Value::Value(const ConstValue &value)
    : ptr_(mg_value_copy(value.ptr())) {}

inline Value::Value(const std::string_view value)
    : Value(
          mg_value_make_string2(mg_string_make2(value.size(), value.data()))) {}

inline Value::Value(const char *value) : Value(mg_value_make_string(value)) {}

inline Value::Value(List &&list) : Value(mg_value_make_list(list.ptr_)) {
  list.ptr_ = nullptr;
}

inline Value::Value(Map &&map) : Value(mg_value_make_map(map.ptr_)) {
  map.ptr_ = nullptr;
}

inline Value::Value(Node &&vertex) : Value(mg_value_make_node(vertex.ptr_)) {
  vertex.ptr_ = nullptr;
}

inline Value::Value(Relationship &&edge)
    : Value(mg_value_make_relationship(edge.ptr_)) {
  edge.ptr_ = nullptr;
}

inline Value::Value(UnboundRelationship &&edge)
    : Value(mg_value_make_unbound_relationship(edge.ptr_)) {
  edge.ptr_ = nullptr;
}

inline Value::Value(Path &&path) : Value(mg_value_make_path(path.ptr_)) {
  path.ptr_ = nullptr;
}

inline Value::Value(Date &&date) : Value(mg_value_make_date(date.ptr_)) {
  date.ptr_ = nullptr;
}

inline Value::Value(Time &&time) : Value(mg_value_make_time(time.ptr_)) {
  time.ptr_ = nullptr;
}

inline Value::Value(LocalTime &&local_time)
    : Value(mg_value_make_local_time(local_time.ptr_)) {
  local_time.ptr_ = nullptr;
}

inline Value::Value(DateTime &&date_time)
    : Value(mg_value_make_date_time(date_time.ptr_)) {
  date_time.ptr_ = nullptr;
}

inline Value::Value(DateTimeZoneId &&date_time_zone_id)
    : Value(mg_value_make_date_time_zone_id(date_time_zone_id.ptr_)) {
  date_time_zone_id.ptr_ = nullptr;
}

inline Value::Value(LocalDateTime &&local_date_time)
    : Value(mg_value_make_local_date_time(local_date_time.ptr_)) {
  local_date_time.ptr_ = nullptr;
}

inline Value::Value(Duration &&duration)
    : Value(mg_value_make_duration(duration.ptr_)) {
  duration.ptr_ = nullptr;
}

inline Value::Value(Point2d &&point_2d)
    : Value(mg_value_make_point_2d(point_2d.ptr_)) {
  point_2d.ptr_ = nullptr;
}

inline Value::Value(Point3d &&point_3d)
    : Value(mg_value_make_point_3d(point_3d.ptr_)) {
  point_3d.ptr_ = nullptr;
}

inline bool Value::ValueBool() const {
  if (type() != Type::Bool) {
    std::abort();
  }
  return static_cast<bool>(mg_value_bool(ptr_));
}

inline int64_t Value::ValueInt() const {
  if (type() != Type::Int) {
    std::abort();
  }
  return mg_value_integer(ptr_);
}

inline double Value::ValueDouble() const {
  if (type() != Type::Double) {
    std::abort();
  }
  return mg_value_float(ptr_);
}

inline std::string_view Value::ValueString() const {
  if (type() != Type::String) {
    std::abort();
  }
  return detail::ConvertString(mg_value_string(ptr_));
}

inline const ConstList Value::ValueList() const {
  if (type() != Type::List) {
    std::abort();
  }
  return ConstList(mg_value_list(ptr_));
}

inline const ConstMap Value::ValueMap() const {
  if (type() != Type::Map) {
    std::abort();
  }
  return ConstMap(mg_value_map(ptr_));
}

inline const ConstNode Value::ValueNode() const {
  if (type() != Type::Node) {
    std::abort();
  }
  return ConstNode(mg_value_node(ptr_));
}

inline const ConstRelationship Value::ValueRelationship() const {
  if (type() != Type::Relationship) {
    std::abort();
  }
  return ConstRelationship(mg_value_relationship(ptr_));
}

inline const ConstUnboundRelationship Value::ValueUnboundRelationship() const {
  if (type() != Type::UnboundRelationship) {
    std::abort();
  }
  return ConstUnboundRelationship(mg_value_unbound_relationship(ptr_));
}

inline const ConstPath Value::ValuePath() const {
  if (type() != Type::Path) {
    std::abort();
  }
  return ConstPath(mg_value_path(ptr_));
}

inline const ConstDate Value::ValueDate() const {
  if (type() != Type::Date) {
    std::abort();
  }
  return ConstDate(mg_value_date(ptr_));
}

inline const ConstTime Value::ValueTime() const {
  if (type() != Type::Time) {
    std::abort();
  }
  return ConstTime(mg_value_time(ptr_));
}

inline const ConstLocalTime Value::ValueLocalTime() const {
  if (type() != Type::LocalTime) {
    std::abort();
  }
  return ConstLocalTime(mg_value_local_time(ptr_));
}

inline const ConstDateTime Value::ValueDateTime() const {
  if (type() != Type::DateTime) {
    std::abort();
  }
  return ConstDateTime(mg_value_date_time(ptr_));
}

inline const ConstDateTimeZoneId Value::ValueDateTimeZoneId() const {
  if (type() != Type::DateTimeZoneId) {
    std::abort();
  }
  return ConstDateTimeZoneId(mg_value_date_time_zone_id(ptr_));
}

inline const ConstLocalDateTime Value::ValueLocalDateTime() const {
  if (type() != Type::LocalDateTime) {
    std::abort();
  }
  return ConstLocalDateTime(mg_value_local_date_time(ptr_));
}

inline const ConstDuration Value::ValueDuration() const {
  if (type() != Type::Duration) {
    std::abort();
  }
  return ConstDuration(mg_value_duration(ptr_));
}

inline const ConstPoint2d Value::ValuePoint2d() const {
  if (type() != Type::Point2d) {
    std::abort();
  }
  return ConstPoint2d(mg_value_point_2d(ptr_));
}

inline const ConstPoint3d Value::ValuePoint3d() const {
  if (type() != Type::Point3d) {
    std::abort();
  }
  return ConstPoint3d(mg_value_point_3d(ptr_));
}

inline Value::Type Value::type() const {
  return detail::ConvertType(mg_value_get_type(ptr_));
}

inline ConstValue Value::AsConstValue() const { return ConstValue(ptr_); }

inline bool Value::operator==(const Value &other) const {
  return detail::AreValuesEqual(ptr_, other.ptr_);
}

inline bool Value::operator==(const ConstValue &other) const {
  return detail::AreValuesEqual(ptr_, other.ptr());
}

inline bool ConstValue::ValueBool() const {
  if (type() != Value::Type::Bool) {
    std::abort();
  }
  return static_cast<bool>(mg_value_bool(const_ptr_));
}

inline int64_t ConstValue::ValueInt() const {
  if (type() != Value::Type::Int) {
    std::abort();
  }
  return mg_value_integer(const_ptr_);
}

inline double ConstValue::ValueDouble() const {
  if (type() != Value::Type::Double) {
    std::abort();
  }
  return mg_value_float(const_ptr_);
}

inline std::string_view ConstValue::ValueString() const {
  if (type() != Value::Type::String) {
    std::abort();
  }
  return detail::ConvertString(mg_value_string(const_ptr_));
}

inline const ConstList ConstValue::ValueList() const {
  if (type() != Value::Type::List) {
    std::abort();
  }
  return ConstList(mg_value_list(const_ptr_));
}

inline const ConstMap ConstValue::ValueMap() const {
  if (type() != Value::Type::Map) {
    std::abort();
  }
  return ConstMap(mg_value_map(const_ptr_));
}

inline const ConstNode ConstValue::ValueNode() const {
  if (type() != Value::Type::Node) {
    std::abort();
  }
  return ConstNode(mg_value_node(const_ptr_));
}

inline const ConstRelationship ConstValue::ValueRelationship() const {
  if (type() != Value::Type::Relationship) {
    std::abort();
  }
  return ConstRelationship(mg_value_relationship(const_ptr_));
}

inline const ConstUnboundRelationship ConstValue::ValueUnboundRelationship()
    const {
  if (type() != Value::Type::UnboundRelationship) {
    std::abort();
  }
  return ConstUnboundRelationship(mg_value_unbound_relationship(const_ptr_));
}

inline const ConstPath ConstValue::ValuePath() const {
  if (type() != Value::Type::Path) {
    std::abort();
  }
  return ConstPath(mg_value_path(const_ptr_));
}

inline Value::Type ConstValue::type() const {
  return detail::ConvertType(mg_value_get_type(const_ptr_));
}

inline const ConstDate ConstValue::ValueDate() const {
  if (type() != Value::Type::Date) {
    std::abort();
  }
  return ConstDate(mg_value_date(const_ptr_));
}

inline const ConstTime ConstValue::ValueTime() const {
  if (type() != Value::Type::Time) {
    std::abort();
  }
  return ConstTime(mg_value_time(const_ptr_));
}

inline const ConstLocalTime ConstValue::ValueLocalTime() const {
  if (type() != Value::Type::LocalTime) {
    std::abort();
  }
  return ConstLocalTime(mg_value_local_time(const_ptr_));
}

inline const ConstDateTime ConstValue::ValueDateTime() const {
  if (type() != Value::Type::DateTime) {
    std::abort();
  }
  return ConstDateTime(mg_value_date_time(const_ptr_));
}

inline const ConstDateTimeZoneId ConstValue::ValueDateTimeZoneId() const {
  if (type() != Value::Type::DateTimeZoneId) {
    std::abort();
  }
  return ConstDateTimeZoneId(mg_value_date_time_zone_id(const_ptr_));
}

inline const ConstLocalDateTime ConstValue::ValueLocalDateTime() const {
  if (type() != Value::Type::LocalDateTime) {
    std::abort();
  }
  return ConstLocalDateTime(mg_value_local_date_time(const_ptr_));
}

inline const ConstDuration ConstValue::ValueDuration() const {
  if (type() != Value::Type::Duration) {
    std::abort();
  }
  return ConstDuration(mg_value_duration(const_ptr_));
}

inline const ConstPoint2d ConstValue::ValuePoint2d() const {
  if (type() != Value::Type::Point2d) {
    std::abort();
  }
  return ConstPoint2d(mg_value_point_2d(const_ptr_));
}

inline const ConstPoint3d ConstValue::ValuePoint3d() const {
  if (type() != Value::Type::Point3d) {
    std::abort();
  }
  return ConstPoint3d(mg_value_point_3d(const_ptr_));
}

inline bool ConstValue::operator==(const ConstValue &other) const {
  return detail::AreValuesEqual(const_ptr_, other.const_ptr_);
}

inline bool ConstValue::operator==(const Value &other) const {
  return detail::AreValuesEqual(const_ptr_, other.ptr());
}

}  // namespace mg
