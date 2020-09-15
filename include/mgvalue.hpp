#pragma once

#include <cstring>
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
class ConstDateTime;
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
// DateTime:

/// \brief Wrapper class for temporal structures
//

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
    Path
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
std::string_view ConvertString(const mg_string *str) {
  return std::string_view(mg_string_data(str), mg_string_size(str));
}

Value::Type ConvertType(mg_value_type type) {
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
    case MG_VALUE_TYPE_UNKNOWN:
      throw std::runtime_error("Unknown value type!");
  }
}

bool AreValuesEqual(const mg_value *value1, const mg_value *value2);

bool AreListsEqual(const mg_list *list1, const mg_list *list2) {
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

bool AreMapsEqual(const mg_map *map1, const mg_map *map2) {
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

bool AreNodesEqual(const mg_node *node1, const mg_node *node2) {
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

bool AreRelationshipsEqual(const mg_relationship *rel1,
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

bool AreUnboundRelationshipsEqual(const mg_unbound_relationship *rel1,
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

bool ArePathsEqual(const mg_path *path1, const mg_path *path2) {
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

bool AreValuesEqual(const mg_value *value1, const mg_value *value2) {
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
    case MG_VALUE_TYPE_UNKNOWN:
      throw std::runtime_error("Comparing values of unknown types!");
  }
}
}  // namespace detail

////////////////////////////////////////////////////////////////////////////////
// List:

ConstValue List::Iterator::operator*() const { return (*iterable_)[index_]; }

List::List(const List &other) : ptr_(mg_list_copy(other.ptr_)) {}

List::List(List &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

List::~List() {
  if (ptr_ != nullptr) {
    mg_list_destroy(ptr_);
  }
}

List::List(const ConstList &list) : ptr_(mg_list_copy(list.ptr())) {}

List::List(const std::vector<mg::Value> &values) : List(values.size()) {
  for (const auto &value : values) {
    Append(value);
  }
}

List::List(std::vector<mg::Value> &&values) : List(values.size()) {
  for (auto &value : values) {
    Append(std::move(value));
  }
}

List::List(std::initializer_list<Value> values) : List(values.size()) {
  for (const auto &value : values) {
    Append(value);
  }
}

const ConstValue List::operator[](size_t index) const {
  return ConstValue(mg_list_at(ptr_, index));
}

bool List::Append(const Value &value) {
  return mg_list_append(ptr_, mg_value_copy(value.ptr())) == 0;
}

bool List::Append(const ConstValue &value) {
  return mg_list_append(ptr_, mg_value_copy(value.ptr())) == 0;
}

bool List::Append(Value &&value) {
  bool result = mg_list_append(ptr_, value.ptr_) == 0;
  value.ptr_ = nullptr;
  return result;
}

const ConstList List::AsConstList() const { return ConstList(ptr_); }

bool List::operator==(const List &other) const {
  return detail::AreListsEqual(ptr_, other.ptr_);
}

bool List::operator==(const ConstList &other) const {
  return detail::AreListsEqual(ptr_, other.ptr());
}

ConstValue ConstList::Iterator::operator*() const {
  return (*iterable_)[index_];
}

const ConstValue ConstList::operator[](size_t index) const {
  return ConstValue(mg_list_at(const_ptr_, index));
}

bool ConstList::operator==(const ConstList &other) const {
  return detail::AreListsEqual(const_ptr_, other.const_ptr_);
}

bool ConstList::operator==(const List &other) const {
  return detail::AreListsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Map:

std::pair<std::string_view, ConstValue> Map::Iterator::operator*() const {
  return std::make_pair(
      detail::ConvertString(mg_map_key_at(iterable_->ptr(), index_)),
      ConstValue(mg_map_value_at(iterable_->ptr(), index_)));
}

Map::Map(const Map &other) : Map(mg_map_copy(other.ptr_)) {}

Map::Map(Map &&other) : Map(other.ptr_) { other.ptr_ = nullptr; }

Map::Map(const ConstMap &map) : ptr_(mg_map_copy(map.ptr())) {}

Map::~Map() {
  if (ptr_ != nullptr) {
    mg_map_destroy(ptr_);
  }
}

Map::Map(std::initializer_list<std::pair<std::string, Value>> list)
    : Map(list.size()) {
  for (const auto &[key, value] : list) {
    Insert(key, value.AsConstValue());
  }
}

ConstValue Map::operator[](const std::string_view key) const {
  return ConstValue(mg_map_at2(ptr_, key.size(), key.data()));
}

Map::Iterator Map::find(const std::string_view key) const {
  for (size_t i = 0; i < size(); ++i) {
    if (key == detail::ConvertString(mg_map_key_at(ptr_, i))) {
      return Iterator(this, i);
    }
  }
  return end();
}

bool Map::Insert(const std::string_view key, const Value &value) {
  return mg_map_insert2(ptr_, mg_string_make2(key.size(), key.data()),
                        mg_value_copy(value.ptr())) == 0;
}

bool Map::Insert(const std::string_view key, const ConstValue &value) {
  return mg_map_insert2(ptr_, mg_string_make2(key.size(), key.data()),
                        mg_value_copy(value.ptr())) == 0;
}

bool Map::Insert(const std::string_view key, Value &&value) {
  bool result = mg_map_insert2(ptr_, mg_string_make2(key.size(), key.data()),
                               value.ptr_) == 0;
  value.ptr_ = nullptr;
  return result;
}

bool Map::InsertUnsafe(const std::string_view key, const Value &value) {
  return mg_map_insert_unsafe2(ptr_, mg_string_make2(key.size(), key.data()),
                               mg_value_copy(value.ptr())) == 0;
}

bool Map::InsertUnsafe(const std::string_view key, const ConstValue &value) {
  return mg_map_insert_unsafe2(ptr_, mg_string_make2(key.size(), key.data()),
                               mg_value_copy(value.ptr())) == 0;
}

bool Map::InsertUnsafe(const std::string_view key, Value &&value) {
  bool result =
      mg_map_insert_unsafe2(ptr_, mg_string_make2(key.size(), key.data()),
                            value.ptr_) == 0;
  value.ptr_ = nullptr;
  return result;
}

const ConstMap Map::AsConstMap() const { return ConstMap(ptr_); }

bool Map::operator==(const Map &other) const {
  return detail::AreMapsEqual(ptr_, other.ptr_);
}

bool Map::operator==(const ConstMap &other) const {
  return detail::AreMapsEqual(ptr_, other.ptr());
}

std::pair<std::string_view, ConstValue> ConstMap::Iterator::operator*() const {
  return std::make_pair(
      detail::ConvertString(mg_map_key_at(iterable_->ptr(), index_)),
      ConstValue(mg_map_value_at(iterable_->ptr(), index_)));
}

ConstValue ConstMap::operator[](const std::string_view key) const {
  return ConstValue(mg_map_at2(const_ptr_, key.size(), key.data()));
}

ConstMap::Iterator ConstMap::find(const std::string_view key) const {
  for (size_t i = 0; i < size(); ++i) {
    if (key == detail::ConvertString(mg_map_key_at(const_ptr_, i))) {
      return Iterator(this, i);
    }
  }
  return end();
}

bool ConstMap::operator==(const ConstMap &other) const {
  return detail::AreMapsEqual(const_ptr_, other.const_ptr_);
}

bool ConstMap::operator==(const Map &other) const {
  return detail::AreMapsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Node:

std::string_view Node::Labels::Iterator::operator*() const {
  return (*iterable_)[index_];
}

std::string_view Node::Labels::operator[](size_t index) const {
  return detail::ConvertString(mg_node_label_at(node_, index));
}

Node::Node(const Node &other) : Node(mg_node_copy(other.ptr_)) {}

Node::Node(Node &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

Node::~Node() {
  if (ptr_ != nullptr) {
    mg_node_destroy(ptr_);
  }
}

Node::Node(const ConstNode &node) : ptr_(mg_node_copy(node.ptr())) {}

bool Node::operator==(const Node &other) const {
  return detail::AreNodesEqual(ptr_, other.ptr_);
}

bool Node::operator==(const ConstNode &other) const {
  return detail::AreNodesEqual(ptr_, other.ptr());
}

ConstNode Node::AsConstNode() const { return ConstNode(ptr_); }

bool ConstNode::operator==(const ConstNode &other) const {
  return detail::AreNodesEqual(const_ptr_, other.const_ptr_);
}

bool ConstNode::operator==(const Node &other) const {
  return detail::AreNodesEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Relationship:

Relationship::Relationship(const Relationship &other)
    : Relationship(mg_relationship_copy(other.ptr_)) {}

Relationship::Relationship(Relationship &&other) : Relationship(other.ptr_) {
  other.ptr_ = nullptr;
}

Relationship::~Relationship() {
  if (ptr_ != nullptr) {
    mg_relationship_destroy(ptr_);
  }
}

Relationship::Relationship(const ConstRelationship &rel)
    : ptr_(mg_relationship_copy(rel.ptr())) {}

std::string_view Relationship::type() const {
  return detail::ConvertString(mg_relationship_type(ptr_));
}

ConstRelationship Relationship::AsConstRelationship() const {
  return ConstRelationship(ptr_);
}

bool Relationship::operator==(const Relationship &other) const {
  return detail::AreRelationshipsEqual(ptr_, other.ptr_);
}

bool Relationship::operator==(const ConstRelationship &other) const {
  return detail::AreRelationshipsEqual(ptr_, other.ptr());
}

std::string_view ConstRelationship::type() const {
  return detail::ConvertString(mg_relationship_type(const_ptr_));
}

bool ConstRelationship::operator==(const ConstRelationship &other) const {
  return detail::AreRelationshipsEqual(const_ptr_, other.const_ptr_);
}

bool ConstRelationship::operator==(const Relationship &other) const {
  return detail::AreRelationshipsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// UnboundRelationship:

UnboundRelationship::UnboundRelationship(const UnboundRelationship &other)
    : ptr_(mg_unbound_relationship_copy(other.ptr_)) {}

UnboundRelationship::UnboundRelationship(UnboundRelationship &&other)
    : ptr_(other.ptr_) {
  other.ptr_ = nullptr;
}

UnboundRelationship::~UnboundRelationship() {
  if (ptr_ != nullptr) {
    mg_unbound_relationship_destroy(ptr_);
  }
}

UnboundRelationship::UnboundRelationship(const ConstUnboundRelationship &rel)
    : ptr_(mg_unbound_relationship_copy(rel.ptr())) {}

std::string_view UnboundRelationship::type() const {
  return detail::ConvertString(mg_unbound_relationship_type(ptr_));
}

ConstUnboundRelationship UnboundRelationship::AsConstUnboundRelationship()
    const {
  return ConstUnboundRelationship(ptr_);
}

bool UnboundRelationship::operator==(const UnboundRelationship &other) const {
  return detail::AreUnboundRelationshipsEqual(ptr_, other.ptr_);
}

bool UnboundRelationship::operator==(
    const ConstUnboundRelationship &other) const {
  return detail::AreUnboundRelationshipsEqual(ptr_, other.ptr());
}

std::string_view ConstUnboundRelationship::type() const {
  return detail::ConvertString(mg_unbound_relationship_type(const_ptr_));
}

bool ConstUnboundRelationship::operator==(
    const ConstUnboundRelationship &other) const {
  return detail::AreUnboundRelationshipsEqual(const_ptr_, other.const_ptr_);
}

bool ConstUnboundRelationship::operator==(
    const UnboundRelationship &other) const {
  return detail::AreUnboundRelationshipsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Path:

Path::Path(const Path &other) : ptr_(mg_path_copy(other.ptr_)) {}

Path::Path(Path &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

Path::~Path() {
  if (ptr_ != nullptr) {
    mg_path_destroy(ptr_);
  }
}

Path::Path(const ConstPath &path) : ptr_(mg_path_copy(path.ptr())) {}

ConstNode Path::GetNodeAt(size_t index) const {
  auto vertex_ptr = mg_path_node_at(ptr_, index);
  if (vertex_ptr == nullptr) {
    std::abort();
  }
  return ConstNode(vertex_ptr);
}

ConstUnboundRelationship Path::GetRelationshipAt(size_t index) const {
  auto edge_ptr = mg_path_relationship_at(ptr_, index);
  if (edge_ptr == nullptr) {
    std::abort();
  }
  return ConstUnboundRelationship(edge_ptr);
}

bool Path::IsReversedRelationshipAt(size_t index) const {
  auto is_reversed = mg_path_relationship_reversed_at(ptr_, index);
  if (is_reversed == -1) {
    std::abort();
  }
  return is_reversed == 1;
}

ConstPath Path::AsConstPath() const { return ConstPath(ptr_); }

bool Path::operator==(const Path &other) const {
  return detail::ArePathsEqual(ptr_, other.ptr_);
}

bool Path::operator==(const ConstPath &other) const {
  return detail::ArePathsEqual(ptr_, other.ptr());
}

ConstNode ConstPath::GetNodeAt(size_t index) const {
  auto vertex_ptr = mg_path_node_at(const_ptr_, index);
  if (vertex_ptr == nullptr) {
    std::abort();
  }
  return ConstNode(vertex_ptr);
}

ConstUnboundRelationship ConstPath::GetRelationshipAt(size_t index) const {
  auto edge_ptr = mg_path_relationship_at(const_ptr_, index);
  if (edge_ptr == nullptr) {
    std::abort();
  }
  return ConstUnboundRelationship(edge_ptr);
}

bool ConstPath::IsReversedRelationshipAt(size_t index) const {
  auto is_reversed = mg_path_relationship_reversed_at(const_ptr_, index);
  if (is_reversed == -1) {
    std::abort();
  }
  return is_reversed == 1;
}

bool ConstPath::operator==(const ConstPath &other) const {
  return detail::ArePathsEqual(const_ptr_, other.const_ptr_);
}

bool ConstPath::operator==(const Path &other) const {
  return detail::ArePathsEqual(const_ptr_, other.ptr());
}

////////////////////////////////////////////////////////////////////////////////
// Value:

Value::Value(const Value &other) : Value(mg_value_copy(other.ptr_)) {}

Value::Value(Value &&other) : ptr_(other.ptr_) { other.ptr_ = nullptr; }

Value::~Value() {
  if (ptr_ != nullptr) {
    mg_value_destroy(ptr_);
  }
}

Value::Value(const ConstValue &value) : ptr_(mg_value_copy(value.ptr())) {}

Value::Value(const std::string_view value)
    : Value(
          mg_value_make_string2(mg_string_make2(value.size(), value.data()))) {}

Value::Value(const char *value) : Value(mg_value_make_string(value)) {}

Value::Value(List &&list) : Value(mg_value_make_list(list.ptr_)) {
  list.ptr_ = nullptr;
}

Value::Value(Map &&map) : Value(mg_value_make_map(map.ptr_)) {
  map.ptr_ = nullptr;
}

Value::Value(Node &&vertex) : Value(mg_value_make_node(vertex.ptr_)) {
  vertex.ptr_ = nullptr;
}

Value::Value(Relationship &&edge)
    : Value(mg_value_make_relationship(edge.ptr_)) {
  edge.ptr_ = nullptr;
}

Value::Value(UnboundRelationship &&edge)
    : Value(mg_value_make_unbound_relationship(edge.ptr_)) {
  edge.ptr_ = nullptr;
}

Value::Value(Path &&path) : Value(mg_value_make_path(path.ptr_)) {
  path.ptr_ = nullptr;
}

bool Value::ValueBool() const {
  if (type() != Type::Bool) {
    std::abort();
  }
  return static_cast<bool>(mg_value_bool(ptr_));
}

int64_t Value::ValueInt() const {
  if (type() != Type::Int) {
    std::abort();
  }
  return mg_value_integer(ptr_);
}

double Value::ValueDouble() const {
  if (type() != Type::Double) {
    std::abort();
  }
  return mg_value_float(ptr_);
}

std::string_view Value::ValueString() const {
  if (type() != Type::String) {
    std::abort();
  }
  return detail::ConvertString(mg_value_string(ptr_));
}

const ConstList Value::ValueList() const {
  if (type() != Type::List) {
    std::abort();
  }
  return ConstList(mg_value_list(ptr_));
}

const ConstMap Value::ValueMap() const {
  if (type() != Type::Map) {
    std::abort();
  }
  return ConstMap(mg_value_map(ptr_));
}

const ConstNode Value::ValueNode() const {
  if (type() != Type::Node) {
    std::abort();
  }
  return ConstNode(mg_value_node(ptr_));
}

const ConstRelationship Value::ValueRelationship() const {
  if (type() != Type::Relationship) {
    std::abort();
  }
  return ConstRelationship(mg_value_relationship(ptr_));
}

const ConstUnboundRelationship Value::ValueUnboundRelationship() const {
  if (type() != Type::UnboundRelationship) {
    std::abort();
  }
  return ConstUnboundRelationship(mg_value_unbound_relationship(ptr_));
}

const ConstPath Value::ValuePath() const {
  if (type() != Type::Path) {
    std::abort();
  }
  return ConstPath(mg_value_path(ptr_));
}

Value::Type Value::type() const {
  return detail::ConvertType(mg_value_get_type(ptr_));
}

ConstValue Value::AsConstValue() const { return ConstValue(ptr_); }

bool Value::operator==(const Value &other) const {
  return detail::AreValuesEqual(ptr_, other.ptr_);
}

bool Value::operator==(const ConstValue &other) const {
  return detail::AreValuesEqual(ptr_, other.ptr());
}

bool ConstValue::ValueBool() const {
  if (type() != Value::Type::Bool) {
    std::abort();
  }
  return static_cast<bool>(mg_value_bool(const_ptr_));
}

int64_t ConstValue::ValueInt() const {
  if (type() != Value::Type::Int) {
    std::abort();
  }
  return mg_value_integer(const_ptr_);
}

double ConstValue::ValueDouble() const {
  if (type() != Value::Type::Double) {
    std::abort();
  }
  return mg_value_float(const_ptr_);
}

std::string_view ConstValue::ValueString() const {
  if (type() != Value::Type::String) {
    std::abort();
  }
  return detail::ConvertString(mg_value_string(const_ptr_));
}

const ConstList ConstValue::ValueList() const {
  if (type() != Value::Type::List) {
    std::abort();
  }
  return ConstList(mg_value_list(const_ptr_));
}

const ConstMap ConstValue::ValueMap() const {
  if (type() != Value::Type::List) {
    std::abort();
  }
  return ConstMap(mg_value_map(const_ptr_));
}

const ConstNode ConstValue::ValueNode() const {
  if (type() != Value::Type::Node) {
    std::abort();
  }
  return ConstNode(mg_value_node(const_ptr_));
}

const ConstRelationship ConstValue::ValueRelationship() const {
  if (type() != Value::Type::Relationship) {
    std::abort();
  }
  return ConstRelationship(mg_value_relationship(const_ptr_));
}

const ConstUnboundRelationship ConstValue::ValueUnboundRelationship() const {
  if (type() != Value::Type::UnboundRelationship) {
    std::abort();
  }
  return ConstUnboundRelationship(mg_value_unbound_relationship(const_ptr_));
}

const ConstPath ConstValue::ValuePath() const {
  if (type() != Value::Type::Path) {
    std::abort();
  }
  return ConstPath(mg_value_path(const_ptr_));
}
Value::Type ConstValue::type() const {
  return detail::ConvertType(mg_value_get_type(const_ptr_));
}

bool ConstValue::operator==(const ConstValue &other) const {
  return detail::AreValuesEqual(const_ptr_, other.const_ptr_);
}

bool ConstValue::operator==(const Value &other) const {
  return detail::AreValuesEqual(const_ptr_, other.ptr());
}

}  // namespace mg
