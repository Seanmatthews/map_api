#ifndef MAP_API_NET_TABLE_TRANSACTION_INL_H_
#define MAP_API_NET_TABLE_TRANSACTION_INL_H_

#include <string>
#include <vector>

namespace map_api {

template <typename IdType>
std::shared_ptr<const Revision> NetTableTransaction::getById(const IdType& id)
    const {
  std::shared_ptr<const Revision> uncommitted = getByIdFromUncommitted(id);
  if (uncommitted) {
    return uncommitted;
  }
  ChunkBase* chunk = chunkOf(id);
  if (!chunk || !workspace_.contains(chunk->id())) {
    return std::shared_ptr<Revision>();
  }
  return getById(id, chunk);
}

template <typename IdType>
std::shared_ptr<const Revision> NetTableTransaction::getById(
    const IdType& id, ChunkBase* chunk) const {
  CHECK_NOTNULL(chunk);
  if (!workspace_.contains(chunk->id())) {
    return std::shared_ptr<Revision>();
  }
  return transactionOf(chunk)->getById(id);
}

template <typename IdType>
std::shared_ptr<const Revision> NetTableTransaction::getByIdFromUncommitted(
    const IdType& id) const {
  std::shared_ptr<const Revision> result;
  for (const TransactionMap::value_type& chunk_transaction :
       chunk_transactions_) {
    result = chunk_transaction.second->getByIdFromUncommitted(id);
    if (result) {
      return result;
    }
  }
  return std::shared_ptr<const Revision>();
}

template <typename ValueType>
void NetTableTransaction::find(int key, const ValueType& value,
                               ConstRevisionMap* result) {
  CHECK_NOTNULL(result);
  // TODO(tcies) Also search in uncommitted.
  // TODO(tcies) Also search in previously committed.
  workspace_.forEachChunk([&, this](const ChunkBase& chunk) {
    ConstRevisionMap chunk_result;
    chunk.constData()->find(key, value, begin_time_, &chunk_result);
    result->insert(chunk_result.begin(), chunk_result.end());
  });
}

template <typename IdType>
void NetTableTransaction::getAvailableIds(std::vector<IdType>* ids) {
  CHECK_NOTNULL(ids)->clear();
  workspace_.forEachChunk([&, this](const ChunkBase& chunk) {
    std::unordered_set<IdType> chunk_result;
    transactionOf(&chunk)->getAvailableIds(&chunk_result);
    ids->insert(ids->end(), chunk_result.begin(), chunk_result.end());
  });
}

template <typename IdType>
std::shared_ptr<const Revision>* NetTableTransaction::getMutableUpdateEntry(
    const IdType& id) {
  // Since we don't know what chunk the update entry is in, we will try all
  // chunks.
  for (TransactionMap::value_type& chunk_transaction : chunk_transactions_) {
    std::shared_ptr<const Revision>* chunk_update_entry;
    if (chunk_transaction.second->getMutableUpdateEntry(id,
                                                        &chunk_update_entry)) {
      return chunk_update_entry;
    }
  }
  LOG(FATAL) << "Tried to update an item which doesn't exist!";
}

template <typename IdType>
void NetTableTransaction::remove(const IdType& id) {
  ChunkBase* chunk = chunkOf(id);
  std::shared_ptr<Revision> remove_revision;
  getById(id, chunk)->copyForWrite(&remove_revision);
  transactionOf(CHECK_NOTNULL(chunk))->remove(remove_revision);
}

template <typename IdType>
ChunkBase* NetTableTransaction::chunkOf(const IdType& id) const {
  // TODO(tcies) uncommitted
  std::shared_ptr<const Revision> latest =
      table_->getById(id, LogicalTime::sample());
  if (!latest) {
    return nullptr;
  } else {
    return table_->getChunk(latest->getChunkId());
  }
}

template <typename TrackerIdType>
void NetTableTransaction::overrideTrackerIdentificationMethod(
    NetTable* tracker_table,
    const std::function<TrackerIdType(const Revision&)>&
        how_to_determine_tracker) {
  CHECK_NOTNULL(tracker_table);
  CHECK(how_to_determine_tracker);
  CHECK_GT(table_->new_chunk_trackers().count(tracker_table), 0u)
      << "Attempted to override a tracker identification method which is "
      << "however not used for pushing new chunk ids.";
  auto determine_map_api_tracker_id = [how_to_determine_tracker](
      const Revision& trackee) {
    return static_cast<common::Id>(how_to_determine_tracker(trackee));
  };

  CHECK(push_new_chunk_ids_to_tracker_overrides_
            .insert(std::make_pair(tracker_table, determine_map_api_tracker_id))
            .second);
}

}  // namespace map_api

#endif  // MAP_API_NET_TABLE_TRANSACTION_INL_H_
