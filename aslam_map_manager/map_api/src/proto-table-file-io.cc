#include <glog/logging.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <map-api/proto-table-file-io.h>
#include <map-api/cr-table.h>
#include <map-api/cru-table.h>

namespace map_api {
ProtoTableFileIO::ProtoTableFileIO(const map_api::NetTable& table,
                                   const std::string& filename)
    : table_name_(table.name()), file_name_(filename) {
  file_.open(filename);
  CHECK(file_.is_open()) << "Couldn't open file " << filename;
}

bool ProtoTableFileIO::StoreTableContents(const map_api::LogicalTime& time,
                                          const map_api::NetTable& table) {
  CHECK_EQ(table_name_, table.name())
      << "You need a separate file-io for every table.";

  map_api::CRTable::RevisionMap revisions;
  // TODO(slynen): Replace by full-history dump.
  map_api::NetTable& table_non_const = const_cast<map_api::NetTable&>(table);
  table_non_const.dumpActiveChunks(time, &revisions);

  CHECK(file_.is_open());

  for (const std::pair<Id, std::shared_ptr<Revision> >& pair : revisions) {
    CHECK(pair.second != nullptr);
    const Revision& revision = *pair.second;

    RevisionStamp current_item_stamp;
    CHECK(revision.get(CRTable::kIdField, &current_item_stamp.first));
    CHECK_EQ(current_item_stamp.first, pair.first);

    bool is_cru =
        revision.get(CRUTable::kUpdateTimeField, &current_item_stamp.second);
    if (!is_cru) {
      CHECK(
          revision.get(CRTable::kInsertTimeField, &current_item_stamp.second));
    }

    bool already_stored = already_stored_items_.count(current_item_stamp) > 0;
    if (!already_stored) {
      // Moving read to the beginning of the file.
      file_.clear();
      file_.seekg(0);
      if (file_.peek() == std::char_traits<char>::eof()) {
        file_.clear();
        file_.seekp(0);
        google::protobuf::io::OstreamOutputStream raw_out(&file_);
        google::protobuf::io::CodedOutputStream coded_out(&raw_out);
        coded_out.WriteLittleEndian32(1);
      } else {
        file_.clear();
        file_.seekg(0);
        // Only creating these once we know the file isn't empty.
        google::protobuf::io::IstreamInputStream raw_in(&file_);
        google::protobuf::io::CodedInputStream coded_in(&raw_in);
        uint32_t message_count;
        coded_in.ReadLittleEndian32(&message_count);

        ++message_count;

        file_.clear();
        file_.seekp(0);
        google::protobuf::io::OstreamOutputStream raw_out(&file_);
        google::protobuf::io::CodedOutputStream coded_out(&raw_out);
        coded_out.WriteLittleEndian32(message_count);
      }

      // Go to end of file and write message size and then the message.
      file_.clear();
      file_.seekp(0, std::ios_base::end);

      google::protobuf::io::OstreamOutputStream raw_out(&file_);
      google::protobuf::io::CodedOutputStream coded_out(&raw_out);

      std::string output_string;
      revision.SerializeToString(&output_string);
      coded_out.WriteVarint32(output_string.size());
      coded_out.WriteRaw(output_string.data(), output_string.size());
    }
  }
  return true;
}

bool ProtoTableFileIO::ReStoreTableContents(map_api::NetTable* table) {
  CHECK_NOTNULL(table);
  CHECK_EQ(table_name_, table->name())
      << "You need a separate file-io for every table.";
  CHECK(file_.is_open());

  file_.clear();
  file_.seekg(0, std::ios::end);
  std::istream::pos_type file_size = file_.tellg();

  file_.clear();
  file_.seekg(0, std::ios::beg);
  google::protobuf::io::IstreamInputStream raw_in(&file_);
  google::protobuf::io::CodedInputStream coded_in(&raw_in);
  coded_in.SetTotalBytesLimit(file_size, file_size);

  uint32_t message_count;
  coded_in.ReadLittleEndian32(&message_count);

  if (message_count == 0) {
    return false;
  }

  map_api::Revision revision;
  for (size_t i = 0; i < message_count; ++i) {
    uint32_t msg_size;
    if (!coded_in.ReadVarint32(&msg_size)) {
      LOG(ERROR) << "Could not read message size."
                 << " while reading message " << i + 1 << " of "
                 << message_count << ".";
      return false;
    }
    if (msg_size == 0) {
      LOG(ERROR) << "Could not read message: size=0."
                 << " while reading message " << i + 1 << " of "
                 << message_count << ".";
      return false;
    }

    std::string input_string;
    if (!coded_in.ReadString(&input_string, msg_size)) {
      LOG(ERROR) << "Could not read message data"
                 << " while reading message " << i + 1 << " of "
                 << message_count << ".";
      return false;
    }

    revision.ParseFromString(input_string);

    // Make sure the table has the chunk that this revision belongs to.
    Id chunk_id;
    CHECK(revision.get(NetTable::kChunkIdField, &chunk_id));
    bool has_chunk = table->has(chunk_id);
    Chunk* chunk = nullptr;
    if (!has_chunk) {
      chunk = table->newChunk(chunk_id);
    } else {
      chunk = table->getChunk(chunk_id);
    }
    CHECK_NOTNULL(chunk);
    table->insert(chunk, &revision);
  }
  return true;
}

}  // namespace map_api
