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

#ifndef MAP_API_LEGACY_CHUNK_DATA_CONTAINER_BASE_H_
#define MAP_API_LEGACY_CHUNK_DATA_CONTAINER_BASE_H_

#include <list>
#include <vector>

#include "map-api/chunk-data-container-base.h"

namespace map_api {

class LegacyChunkDataContainerBase : public ChunkDataContainerBase {
  friend class LegacyChunk;

 public:
  // ======
  // CREATE
  // ======

  /**
   * Pointer to query, as it is modified according to the default field policies
   * of the respective implementation. This implementation wrapper checks table
   * and query for sanity before calling the implementation:
   * - Table initialized?
   * - Do query and table structure match?
   * - Set default fields.
   *
   * The bulk flavor is for bundling multiple inserts into one transaction,
   * for performance reasons. It also allows specifying the time of insertion,
   * for singular transaction commit times.
   * TODO(tcies) make void where possible
   */
  virtual bool insert(const LogicalTime& time,
                      const std::shared_ptr<Revision>& query) final;
  virtual bool bulkInsert(const LogicalTime& time,
                          const MutableRevisionMap& query) final;
  /**
   * Unlike insert, patch does not modify the query, but assumes that all
   * default values are set correctly.
   */
  virtual bool patch(const std::shared_ptr<const Revision>& revision) final;

  class History : public std::list<std::shared_ptr<const Revision> > {
   public:
    virtual ~History();
    inline const_iterator latestAt(const LogicalTime& time) const;
  };

  // ============
  // READ HISTORY
  // ============
  template <typename IdType>
  void itemHistory(const IdType& id, const LogicalTime& time,
                   History* dest) const;
  typedef std::unordered_map<map_api_common::Id, History> HistoryMap;
  template <typename ValueType>
  void findHistory(int key, const ValueType& value, const LogicalTime& time,
                   HistoryMap* dest) const;
  virtual void findHistoryByRevision(int key, const Revision& valueHolder,
                                     const LogicalTime& time,
                                     HistoryMap* dest) const final;

  // ======
  // UPDATE
  // ======
  /**
   * Field ID in revision must correspond to an already present item, revision
   * structure needs to match. Query may be modified according to the default
   * field policy.
   */
  void update(const LogicalTime& time, const std::shared_ptr<Revision>& query);

  // ======
  // DELETE
  // ======
  void remove(const LogicalTime& time, const std::shared_ptr<Revision>& query);
  // avoid if possible - this is slower
  template <typename IdType>
  void remove(const LogicalTime& time, const IdType& id);
  void clear();

 private:
  // =====================================
  // READ OPERATIONS INHERITED FROM PARENT
  // =====================================
  virtual bool initImpl() = 0;
  virtual std::shared_ptr<const Revision> getByIdImpl(
      const map_api_common::Id& id, const LogicalTime& time) const = 0;
  // If key is -1, this should return all the data in the table.
  virtual void findByRevisionImpl(int key, const Revision& valueHolder,
                                  const LogicalTime& time,
                                  ConstRevisionMap* dest) const = 0;
  virtual void getAvailableIdsImpl(const LogicalTime& time,
                                   std::vector<map_api_common::Id>* ids) const = 0;
  // If key is -1, this should return all the data in the table.
  virtual int countByRevisionImpl(int key, const Revision& valueHolder,
                                  const LogicalTime& time) const = 0;

  // ======================================
  // LEGACY-CHUNK-SPECIFIC WRITE OPERATIONS
  // ======================================
  virtual bool insertImpl(const std::shared_ptr<const Revision>& query) = 0;
  // TODO(tcies) The best thing would be to have "query" be a ConstRevisionMap,
  // but unfortunately, that would require casting from Mutable, as the
  // revision map is mutable on the caller side.
  virtual bool bulkInsertImpl(const MutableRevisionMap& query) = 0;
  virtual bool patchImpl(const std::shared_ptr<const Revision>& query) = 0;
  // If key is -1, this should return all the data in the table.
  virtual void findHistoryByRevisionImpl(int key, const Revision& valueHolder,
                                         const LogicalTime& time,
                                         HistoryMap* dest) const = 0;
  virtual void chunkHistory(const map_api_common::Id& chunk_id, const LogicalTime& time,
                            HistoryMap* dest) const = 0;
  virtual void itemHistoryImpl(const map_api_common::Id& id, const LogicalTime& time,
                               History* dest) const = 0;
  virtual bool insertUpdatedImpl(const std::shared_ptr<Revision>& query) = 0;
  virtual void clearImpl() = 0;
};

}  // namespace map_api

#include "map-api/legacy-chunk-data-container-base-inl.h"

#endif  // MAP_API_LEGACY_CHUNK_DATA_CONTAINER_BASE_H_
