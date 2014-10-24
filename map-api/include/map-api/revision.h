#ifndef MAP_API_REVISION_H_
#define MAP_API_REVISION_H_

#include <memory>
#include <unordered_map>
#include <set>
#include <string>

#include <glog/logging.h>
#include <Poco/Data/BLOB.h>
#include <Poco/Data/Statement.h>

#include "./core.pb.h"
#include <map-api/logical-time.h>
#include <map-api/unique-id.h>

namespace map_api {

class Revision {
  friend class Chunk;
  friend class CRTable;
  friend class CRTableRamMap;
  friend class CRUTable;
  template<int BlockSize>
  friend class STXXLRevisionStore;

 public:
  explicit Revision(const std::shared_ptr<proto::Revision>& revision);
  explicit Revision(const Revision& other);
  Revision& operator=(const Revision& other) = delete;
  /**
   * Insert placeholder in SQLite insert statements. Returns blob shared pointer
   * for dynamically created blob objects
   */
  std::shared_ptr<Poco::Data::BLOB> insertPlaceHolder(
      int index, Poco::Data::Statement* stat) const;

  template <typename FieldType>
  static proto::Type getProtobufTypeEnum();

  void addField(int index, proto::Type type);
  template <typename FieldType>
  void addField(int index);

  /**
   * Does not check type - type is checked with get/set. Nothing that can be
   * done if type doesn't match anyways.
   */
  bool hasField(int index) const;

  proto::Type getFieldType(int index) const;

  template <typename FieldType>
  bool set(int index, const FieldType& value);

  template <typename FieldType>
  bool get(int index, FieldType* value) const;

  inline LogicalTime getInsertTime() const {
    return LogicalTime(underlying_revision_->insert_time());
  }
  inline LogicalTime getUpdateTime() const {
    return LogicalTime(underlying_revision_->update_time());
  }
  inline LogicalTime getModificationTime() const {
    return (underlying_revision_->has_update_time()) ? getUpdateTime()
                                                     : getInsertTime();
  }
  inline Id getChunkId() const {
    Id id;
    id.fromHexString(underlying_revision_->chunk_id().hash());
    return id;
  }
  template <typename IdType>
  inline IdType getId() const {
    IdType id;
    id.fromHexString(underlying_revision_->id().hash());
    return id;
  }
  template <typename IdType>
  inline void setId(const IdType& id) {
    underlying_revision_->mutable_id()->set_hash(id.hexString());
  }
  inline bool isRemoved() const {
    return
        underlying_revision_->has_removed() && underlying_revision_->removed();
  }

  template <typename ExpectedType>
  bool verifyEqual(int index, const ExpectedType& expected) const;

  /**
   * Returns true if Revision contains same fields as other
   */
  bool structureMatch(const Revision& reference) const;

  /**
   * Returns true if value at key is same as with other
   */
  bool fieldMatch(const Revision& other, int index) const;

  std::string dumpToString() const;

  inline std::string serializeUnderlying() const {
    return underlying_revision_->SerializeAsString();
  }

  inline bool SerializeToCodedStream(
      google::protobuf::io::CodedOutputStream* output) const {
    CHECK_NOTNULL(output);
    return underlying_revision_->SerializeToCodedStream(output);
  }

  inline int byteSize() const { return underlying_revision_->ByteSize(); }

  inline bool parse(const std::string& origin) {
    return underlying_revision_->ParseFromString(origin);
  }

  inline int customFieldCount() const {
    return underlying_revision_->custom_field_values_size();
  }

  bool operator==(const Revision& other) const;
  inline bool operator!=(const Revision& other) const {
    return !operator==(other);
  }

 private:
  inline void setInsertTime(const LogicalTime& time) {
    underlying_revision_->set_insert_time(time.serialize());
  }
  inline void setUpdateTime(const LogicalTime& time) {
    underlying_revision_->set_update_time(time.serialize());
  }
  inline void setChunkId(const Id& id) {  // TODO(tcies) mutable, zerocopy
    underlying_revision_->mutable_chunk_id()->set_hash(id.hexString());
  }
  inline void setRemoved() { underlying_revision_->set_removed(true); }

  // exception to parameter ordering: The standard way would make the function
  // call ambiguous if FieldType = int
  template <typename FieldType>
  bool set(proto::TableField* field, const FieldType& value);

  template <typename FieldType>
  bool get(const proto::TableField& field, FieldType* value) const;

  std::shared_ptr<proto::Revision> underlying_revision_;
};

/**
 * Convenience macros to specialize the above templates in one line.
 */
#define MAP_API_TYPE_ENUM(TYPE, ENUM)                 \
  template <>                                         \
  proto::Type Revision::getProtobufTypeEnum<TYPE>() { \
    return ENUM;                                      \
  }                                                   \
  extern void revEnum##__FILE__##__LINE__(void)

#define MAP_API_REVISION_SET(TYPE) \
  template <>                      \
  bool Revision::set<TYPE>(proto::TableField* field, const TYPE& value)

#define MAP_API_REVISION_GET(TYPE) \
  template <>                      \
  bool Revision::get<TYPE>(const proto::TableField& field, TYPE* value) const

/**
 * One Macro to define REVISION_ENUM, _SET and _GET for Protobuf objects
 */
#define MAP_API_REVISION_PROTOBUF(TYPE)                              \
  MAP_API_TYPE_ENUM(TYPE, ::map_api::proto::Type::BLOB);             \
                                                                     \
  MAP_API_REVISION_SET(TYPE) {                                       \
    CHECK_NOTNULL(field)->set_blob_value(value.SerializeAsString()); \
    return true;                                                     \
  }                                                                  \
                                                                     \
  MAP_API_REVISION_GET(TYPE) {                                       \
    CHECK_NOTNULL(value);                                            \
    bool parsed = value->ParseFromString(field.blob_value());        \
    if (!parsed) {                                                   \
      LOG(ERROR) << "Failed to parse " << #TYPE;                     \
      return false;                                                  \
    }                                                                \
    return true;                                                     \
  }                                                                  \
  extern void __FILE__##__LINE__(void)
/**
 * Same for UniqueId derivates
 */
#define MAP_API_REVISION_UNIQUE_ID(TypeName)                          \
  MAP_API_TYPE_ENUM(TypeName, ::map_api::proto::Type::HASH128);       \
  MAP_API_REVISION_SET(TypeName) {                                    \
    CHECK_NOTNULL(field)->set_string_value(value.hexString());        \
    return true;                                                      \
  }                                                                   \
  MAP_API_REVISION_GET(TypeName) {                                    \
    return CHECK_NOTNULL(value)->fromHexString(field.string_value()); \
  }                                                                   \
  extern void __FILE__##__LINE__(void)

/**
 * A generic, blob-y field type for testing blob insertion
 */
class testBlob : public map_api::proto::TableField {
 public:
  inline bool operator==(const testBlob& other) const {
    if (!this->has_type()) return !other.has_type();
    return type() == other.type();
  }
};

}  // namespace map_api

#include "./revision-inl.h"

#endif  // MAP_API_REVISION_H_