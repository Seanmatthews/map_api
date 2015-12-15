#ifndef MAP_API_PROTO_TABLE_FILE_IO_H_
#define MAP_API_PROTO_TABLE_FILE_IO_H_

#include <fstream>  // NOLINT
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <google/protobuf/io/gzip_stream.h>
#include <multiagent-mapping-common/unique-id.h>

#include "map-api/logical-time.h"

namespace map_api {
class ChunkBase;
class ConstRevisionMap;
class NetTable;
class Transaction;

typedef std::pair<common::Id, map_api::LogicalTime> RevisionStamp;
}  // namespace map_api

namespace std {
template <>
struct hash<map_api::RevisionStamp> {
  std::hash<common::Id> id_hasher;
  std::hash<map_api::LogicalTime> time_hasher;
  std::size_t operator()(const map_api::RevisionStamp& stamp) const {
    return id_hasher(stamp.first) ^ time_hasher(stamp.second);
  }
};
}  // namespace std

// Stores all revisions from a table to a file.
namespace map_api {
class ProtoTableFileIO {
  static constexpr std::ios_base::openmode kDefaultOpenmode =
      std::fstream::binary | std::ios_base::in | std::ios_base::out;
  static constexpr std::ios_base::openmode kReadOnlyOpenMode =
      std::fstream::binary | std::ios_base::in;
  static constexpr std::ios_base::openmode kTruncateOpenMode =
      std::fstream::binary | std::ios_base::in | std::ios_base::out |
      std::fstream::trunc;

 public:
  ProtoTableFileIO(const std::string& filename, map_api::NetTable* table);
  ~ProtoTableFileIO();
  bool storeTableContents(const map_api::LogicalTime& time);
  bool storeTableContents(const ConstRevisionMap& revisions,
                          const std::vector<common::Id>& ids_to_store);
  bool restoreTableContents();
  bool restoreTableContents(
      map_api::Transaction* transaction,
      std::unordered_map<common::Id, ChunkBase*>* existing_chunks,
      std::mutex* existing_chunks_mutex);
  void truncFile();

 private:
  static constexpr google::protobuf::io::GzipOutputStream::Format kOutFormat =
      google::protobuf::io::GzipOutputStream::Format::GZIP;
  static constexpr google::protobuf::io::GzipInputStream::Format kInFormat =
      google::protobuf::io::GzipInputStream::Format::GZIP;
  static constexpr int kZipBufferSize = 64;
  static constexpr int kZipCompressionLevel = -1;
  google::protobuf::io::GzipOutputStream::Options zip_options_;

  std::string file_name_;
  map_api::NetTable* table_;
  std::fstream file_;
  std::unordered_set<RevisionStamp> already_stored_items_;
  bool read_only_mode_;
};
}  // namespace map_api
#endif  // MAP_API_PROTO_TABLE_FILE_IO_H_
