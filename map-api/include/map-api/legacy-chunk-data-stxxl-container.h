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

#ifndef MAP_API_LEGACY_CHUNK_DATA_STXXL_CONTAINER_H_
#define MAP_API_LEGACY_CHUNK_DATA_STXXL_CONTAINER_H_

#include <list>
#include <vector>

#include "map-api/legacy-chunk-data-container-base.h"
#include "map-api/stxxl-revision-store.h"

namespace map_api {

class LegacyChunkDataStxxlContainer : public LegacyChunkDataContainerBase {
 public:
  LegacyChunkDataStxxlContainer();
  virtual ~LegacyChunkDataStxxlContainer();

 private:
  virtual bool initImpl() final override;
  virtual bool insertImpl(const std::shared_ptr<const Revision>& query)
      final override;
  virtual bool bulkInsertImpl(const MutableRevisionMap& query) final override;
  virtual bool patchImpl(const std::shared_ptr<const Revision>& query)
      final override;
  virtual std::shared_ptr<const Revision> getByIdImpl(
      const map_api_common::Id& id, const LogicalTime& time) const final override;
  virtual void findByRevisionImpl(int key, const Revision& valueHolder,
                                  const LogicalTime& time,
                                  ConstRevisionMap* dest) const final override;
  virtual int countByRevisionImpl(int key, const Revision& valueHolder,
                                  const LogicalTime& time) const final override;
  virtual void getAvailableIdsImpl(const LogicalTime& time,
                                   std::vector<map_api_common::Id>* ids) const
      final override;
  virtual bool insertUpdatedImpl(const std::shared_ptr<Revision>& query)
      final override;
  virtual void findHistoryByRevisionImpl(int key, const Revision& valueHolder,
                                         const LogicalTime& time,
                                         HistoryMap* dest) const final override;
  virtual void chunkHistory(const map_api_common::Id& chunk_id, const LogicalTime& time,
                            HistoryMap* dest) const final override;
  virtual void itemHistoryImpl(const map_api_common::Id& id, const LogicalTime& time,
                               History* dest) const final override;
  virtual void clearImpl() final override;

  inline void forEachItemFoundAtTime(
      int key, const Revision& value_holder, const LogicalTime& time,
      const std::function<void(const map_api_common::Id& id,
                               const Revision::ConstPtr& item)>& action) const;
  inline void forChunkItemsAtTime(
      const map_api_common::Id& chunk_id, const LogicalTime& time,
      const std::function<void(const map_api_common::Id& id,
                               const Revision::ConstPtr& item)>& action) const;
  inline void trimToTime(const LogicalTime& time, HistoryMap* subject) const;

  class STXXLHistory : public std::list<CRURevisionInformation> {
   public:
    inline const_iterator latestAt(const LogicalTime& time) const {
      for (const_iterator it = cbegin(); it != cend(); ++it) {
        if (it->update_time_ <= time) {
          return it;
        }
      }
      return cend();
    }
  };
  typedef std::unordered_map<map_api_common::Id, STXXLHistory> STXXLHistoryMap;
  STXXLHistoryMap data_;

  static constexpr int kBlockSize = kSTXXLDefaultBlockSize;
  std::unique_ptr<STXXLRevisionStore<kBlockSize>> revision_store_;
};

}  // namespace map_api

#include "map-api/legacy-chunk-data-container-base-inl.h"

#endif  // MAP_API_LEGACY_CHUNK_DATA_STXXL_CONTAINER_H_
