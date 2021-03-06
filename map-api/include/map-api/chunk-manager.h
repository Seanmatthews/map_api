// Copyright (C) 2014-2017 Titus Cieslewski, ASL, ETH Zurich, Switzerland
// You can contact the author at <titus at ifi dot uzh dot ch>
// Copyright (C) 2014-2015 Simon Lynen, ASL, ETH Zurich, Switzerland
// Copyright (c) 2014-2015, Marcin Dymczyk, ASL, ETH Zurich, Switzerland
// Copyright (c) 2014, Stéphane Magnenat, ASL, ETH Zurich, Switzerland
//
// This file is part of Map API.
//
// Map API is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Map API is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Map API. If not, see <http://www.gnu.org/licenses/>.

#ifndef MAP_API_CHUNK_MANAGER_H_
#define MAP_API_CHUNK_MANAGER_H_

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <map-api-common/unique-id.h>
#include "./net-table.pb.h"

namespace map_api {
class ChunkBase;
class NetTable;
class Revision;

class ChunkManagerBase {
 public:
  explicit ChunkManagerBase(map_api::NetTable* underlying_table)
      : underlying_table_(CHECK_NOTNULL(underlying_table)) {}

  virtual ~ChunkManagerBase() {}

  // Returns the chunk in which the given item can be placed.
  virtual ChunkBase* getChunkForItem(const Revision& revision) = 0;

  inline map_api::NetTable* getUnderlyingTable() {
    return underlying_table_;
  };

  inline size_t numChunks() const { return active_chunks_.size(); }

  inline void getChunkIds(std::set<map_api_common::Id>* chunk_ids) const {
    CHECK_NOTNULL(chunk_ids);
    chunk_ids->clear();
    for (const std::pair<const map_api_common::Id, ChunkBase*>& pair : active_chunks_) {
      chunk_ids->emplace(pair.first);
    }
  }
  inline void getChunkIds(map_api_common::IdSet* chunk_ids) const {
    CHECK_NOTNULL(chunk_ids);
    chunk_ids->clear();
    chunk_ids->rehash(active_chunks_.size());
    for (const std::pair<const map_api_common::Id, ChunkBase*>& pair : active_chunks_) {
      chunk_ids->emplace(pair.first);
    }
  }
  inline void getChunkIds(proto::ChunkIdList* chunk_id_list) const {
    CHECK_NOTNULL(chunk_id_list);
    chunk_id_list->clear_chunk_ids();
    for (const std::pair<const map_api_common::Id, ChunkBase*>& pair : active_chunks_) {
      pair.first.serialize(chunk_id_list->add_chunk_ids());
    }
  }

  void requestParticipationAllChunks();

 protected:
  map_api::NetTable* underlying_table_;
  std::unordered_map<map_api_common::Id, ChunkBase*> active_chunks_;
};

static constexpr int kDefaultChunkSizeBytes = 20 * 1024 * 1024;
// A Chunk manager that splits chunks based on their size.
class ChunkManagerChunkSize : public ChunkManagerBase {
 public:
  ChunkManagerChunkSize(int max_chunk_size_bytes,
                        map_api::NetTable* underlying_table)
      : ChunkManagerBase(CHECK_NOTNULL(underlying_table)),
        max_chunk_size_bytes_(max_chunk_size_bytes),
        current_chunk_(nullptr),
        current_chunk_size_bytes_(0) {}
  ~ChunkManagerChunkSize() {}

  virtual ChunkBase* getChunkForItem(const Revision& revision);

 private:
  int max_chunk_size_bytes_;
  ChunkBase* current_chunk_;
  int current_chunk_size_bytes_;
};

}  // namespace map_api
#endif  // MAP_API_CHUNK_MANAGER_H_
