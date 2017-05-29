#include "map-api/legacy-chunk-data-ram-container.h"

namespace map_api {

LegacyChunkDataRamContainer::~LegacyChunkDataRamContainer() {}

bool LegacyChunkDataRamContainer::initImpl() { return true; }

bool LegacyChunkDataRamContainer::insertImpl(const Revision::ConstPtr& query) {
  CHECK(query != nullptr);
  map_api_common::Id id = query->getId<map_api_common::Id>();
  HistoryMap::iterator found = data_.find(id);
  if (found != data_.end()) {
    return false;
  }
  data_[id].push_front(query);
  return true;
}

bool LegacyChunkDataRamContainer::bulkInsertImpl(
    const MutableRevisionMap& query) {
  for (const MutableRevisionMap::value_type& pair : query) {
    if (data_.find(pair.first) != data_.end()) {
      return false;
    }
  }
  for (const MutableRevisionMap::value_type& pair : query) {
    data_[pair.first].push_front(pair.second);
  }
  return true;
}

bool LegacyChunkDataRamContainer::patchImpl(const Revision::ConstPtr& query) {
  CHECK(query != nullptr);
  map_api_common::Id id = query->getId<map_api_common::Id>();
  LogicalTime time = query->getUpdateTime();
  HistoryMap::iterator found = data_.find(id);
  if (found == data_.end()) {
    found = data_.insert(std::make_pair(id, History())).first;
  }
  for (History::iterator it = found->second.begin(); it != found->second.end();
       ++it) {
    if ((*it)->getUpdateTime() <= time) {
      CHECK_NE(time, (*it)->getUpdateTime());
      found->second.insert(it, query);
      return true;
    }
    LOG(WARNING) << "Patching, not in front!";  // shouldn't usually be the case
  }
  found->second.push_back(query);
  return true;
}

Revision::ConstPtr LegacyChunkDataRamContainer::getByIdImpl(
    const map_api_common::Id& id, const LogicalTime& time) const {
  HistoryMap::const_iterator found = data_.find(id);
  if (found == data_.end()) {
    return std::shared_ptr<Revision>();
  }
  History::const_iterator latest = found->second.latestAt(time);
  if (latest == found->second.end()) {
    return Revision::ConstPtr();
  }
  return *latest;
}

void LegacyChunkDataRamContainer::findByRevisionImpl(
    int key, const Revision& value_holder, const LogicalTime& time,
    ConstRevisionMap* dest) const {
  CHECK_NOTNULL(dest);
  dest->clear();
  forEachItemFoundAtTime(
      key, value_holder, time,
      [&dest](const map_api_common::Id& id, const Revision::ConstPtr& item) {
        CHECK(dest->find(id) == dest->end());
        CHECK(dest->emplace(id, item).second);
      });
}

void LegacyChunkDataRamContainer::getAvailableIdsImpl(
    const LogicalTime& time, std::vector<map_api_common::Id>* ids) const {
  CHECK_NOTNULL(ids);
  ids->clear();
  ids->reserve(data_.size());
  for (const HistoryMap::value_type& pair : data_) {
    History::const_iterator latest = pair.second.latestAt(time);
    if (latest != pair.second.cend()) {
      ids->emplace_back(pair.first);
    }
  }
}

int LegacyChunkDataRamContainer::countByRevisionImpl(
    int key, const Revision& value_holder, const LogicalTime& time) const {
  int count = 0;
  forEachItemFoundAtTime(
      key, value_holder, time,
      [&count](const map_api_common::Id& /*id*/,
               const Revision::ConstPtr& /*item*/) { ++count; });
  return count;
}

bool LegacyChunkDataRamContainer::insertUpdatedImpl(
    const std::shared_ptr<Revision>& query) {
  return patchImpl(query);
}

void LegacyChunkDataRamContainer::findHistoryByRevisionImpl(
    int key, const Revision& valueHolder, const LogicalTime& time,
    HistoryMap* dest) const {
  CHECK_NOTNULL(dest);
  dest->clear();
  for (const HistoryMap::value_type& pair : data_) {
    // using current state for filter
    if (key < 0 || valueHolder.fieldMatch(**pair.second.begin(), key)) {
      CHECK(dest->insert(pair).second);
    }
  }
  trimToTime(time, dest);
}

void LegacyChunkDataRamContainer::chunkHistory(const map_api_common::Id& chunk_id,
                                               const LogicalTime& time,
                                               HistoryMap* dest) const {
  CHECK_NOTNULL(dest)->clear();
  for (const HistoryMap::value_type& pair : data_) {
    if ((*pair.second.begin())->getChunkId() == chunk_id) {
      CHECK(dest->emplace(pair).second);
    }
  }
  trimToTime(time, dest);
}

void LegacyChunkDataRamContainer::itemHistoryImpl(const map_api_common::Id& id,
                                                  const LogicalTime& time,
                                                  History* dest) const {
  CHECK_NOTNULL(dest)->clear();
  HistoryMap::const_iterator found = data_.find(id);
  CHECK(found != data_.end());
  *dest = History(found->second);
  dest->remove_if([&time](const Revision::ConstPtr& item) {
    return item->getUpdateTime() > time;
  });
}

void LegacyChunkDataRamContainer::clearImpl() { data_.clear(); }

inline void LegacyChunkDataRamContainer::forEachItemFoundAtTime(
    int key, const Revision& value_holder, const LogicalTime& time,
    const std::function<void(const map_api_common::Id& id,
                             const Revision::ConstPtr& item)>& action) const {
  for (const HistoryMap::value_type& pair : data_) {
    History::const_iterator latest = pair.second.latestAt(time);
    if (latest != pair.second.cend()) {
      if (key < 0 || value_holder.fieldMatch(**latest, key)) {
        action(pair.first, *latest);
      }
    }
  }
}

inline void LegacyChunkDataRamContainer::forChunkItemsAtTime(
    const map_api_common::Id& chunk_id, const LogicalTime& time,
    const std::function<void(const map_api_common::Id& id,
                             const Revision::ConstPtr& item)>& action) const {
  for (const HistoryMap::value_type& pair : data_) {
    if ((*pair.second.begin())->getChunkId() == chunk_id) {
      History::const_iterator latest = pair.second.latestAt(time);
      if (latest != pair.second.cend()) {
        action(pair.first, *latest);
      }
    }
  }
}

inline void LegacyChunkDataRamContainer::trimToTime(const LogicalTime& time,
                                                    HistoryMap* subject) const {
  CHECK_NOTNULL(subject);
  for (HistoryMap::value_type& pair : *subject) {
    pair.second.remove_if([&time](const Revision::ConstPtr& item) {
      return item->getUpdateTime() > time;
    });
  }
}

} // namespace map_api
