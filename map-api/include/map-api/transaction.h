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

#ifndef MAP_API_TRANSACTION_H_
#define MAP_API_TRANSACTION_H_

#include <future>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glog/logging.h>

#include "map-api/internal/commit-future.h"
#include "map-api/logical-time.h"
#include "map-api/net-table-transaction.h"
#include "map-api/workspace.h"

namespace map_api {
class CacheBase;
class ChunkBase;
class ChunkManagerBase;
class ConflictMap;
class NetTable;
template <typename IdType>
class NetTableTransactionInterface;
class Revision;
template <typename IdType, typename ObjectType>
class ThreadsafeCache;

namespace proto {
class Revision;
}  // namespace proto

class Transaction {
  friend class CacheBase;
  template <typename IdType, typename ObjectType>
  friend class ThreadsafeCache;

 public:
  Transaction(const std::shared_ptr<Workspace>& workspace,
              const LogicalTime& begin_time);
  // Defaults: Full workspace, current time.
  Transaction();
  explicit Transaction(const std::shared_ptr<Workspace>& workspace);
  explicit Transaction(const LogicalTime& begin_time);
  // Build a transaction based on the promise that another transaction, which
  // has not yet committed, will succeed in committing. Allows pipelining
  // processing and network transmission.
  typedef std::unordered_map<NetTable*, NetTableTransaction::CommitFutureTree>
      CommitFutureTree;
  explicit Transaction(const CommitFutureTree& commit_futures);
  Transaction(const std::shared_ptr<Workspace>& workspace,
              const LogicalTime& begin_time,
              const CommitFutureTree* commit_futures);

  ~Transaction();

  // ====
  // READ
  // ====
  inline LogicalTime getBeginTime() const { return begin_time_; }
  /**
   * By Id or chunk:
   * Use the overload with chunk specification to increase performance. Use
   * dumpChunk() for best performance if reading out most of a chunk.
   */
  template <typename IdType>
  std::shared_ptr<const Revision> getById(const IdType& id,
                                          NetTable* table) const;
  template <typename IdType>
  std::shared_ptr<const Revision> getById(const IdType& id, NetTable* table,
                                          ChunkBase* chunk) const;
  void dumpChunk(NetTable* table, ChunkBase* chunk, ConstRevisionMap* result);
  void dumpActiveChunks(NetTable* table, ConstRevisionMap* result);
  template <typename IdType>
  void getAvailableIds(NetTable* table, std::vector<IdType>* ids);
  /**
   * By some other field: Searches in ALL active chunks of a table, thus
   * fundamentally differing from getById or dumpChunk.
   */
  template <typename ValueType>
  void find(int key, const ValueType& value, NetTable* table,
            ConstRevisionMap* result);
  bool fetchAllChunksTrackedByItemsInTable(NetTable* const table);
  template <typename IdType>
  void fetchAllChunksTrackedBy(const IdType& id, NetTable* const table);

  // =====
  // WRITE
  // =====
  void insert(NetTable* table, ChunkBase* chunk,
              std::shared_ptr<Revision> revision);
  /**
   * Uses ChunkManager to auto-size chunks.
   */
  void insert(ChunkManagerBase* chunk_manager,
              std::shared_ptr<Revision> revision);
  void update(NetTable* table, std::shared_ptr<Revision> revision);
  // fast
  void remove(NetTable* table, std::shared_ptr<Revision> revision);
  // slow
  template <typename IdType>
  void remove(const IdType& id, NetTable* table);

  // ======================
  // TRANSACTION OPERATIONS
  // ======================
  bool commit();
  // Blocks until checks are performed, but does not block on network
  // transmission. Parallel commit can't currently be combined with
  // multi-commit. Commit futures assume that the transactions they have been
  // created from don't change any more.
  bool commitInParallel(CommitFutureTree* future_tree);
  void joinParallelCommitIfRunning();
  inline LogicalTime getCommitTime() const { return commit_time_; }
  // Requires specialization of
  // std::string getComparisonString(const ObjectType& a, const ObjectType& b);
  // or
  // std::string ObjectType::getComparisonString(const ObjectType&) const;
  // Note that the latter will be correctly called for shared pointers.
  template <typename ObjectType>
  std::string debugConflictsInTable(NetTable* table);
  /**
   * Merge_transaction will be filled with all insertions and non-conflicting
   * updates from this transaction, while the conflicting updates will be
   * represented in a ConflictMap.
   */
  void merge(const std::shared_ptr<Transaction>& merge_transaction,
             ConflictMap* conflicts);
  void detachFutures();

  // ==========
  // STATISTICS
  // ==========
  size_t numChangedItems() const;

  // ======
  // CACHES
  // ======
  template <typename IdType, typename ObjectType>
  std::shared_ptr<ThreadsafeCache<IdType, ObjectType>> createCache(
      NetTable* table);
  template <typename IdType, typename ObjectType>
  const ThreadsafeCache<IdType, ObjectType>& getCache(NetTable* table);
  template <typename IdType, typename ObjectType>
  void setCacheUpdateFilter(
      const std::function<bool(const ObjectType& original,  // NOLINT
                               const ObjectType& innovation)>& update_filter,
      NetTable* table);

  // =============
  // MISCELLANEOUS
  // =============
  template <typename TrackerIdType>
  void overrideTrackerIdentificationMethod(
      NetTable* trackee_table, NetTable* tracker_table,
      const std::function<TrackerIdType(const Revision&)>&
          how_to_determine_tracker);
  // The following must be called if chunks are fetched after the transaction
  // has been initialized, otherwise the new items can't be fetched by the
  // transaction.
  void refreshIdToChunkIdMaps();
  // Same, for the caches.
  void refreshAvailableIdsInCaches();

 private:
  void enableDirectAccess();
  void disableDirectAccess();

  NetTableTransaction* transactionOf(NetTable* table) const;

  void ensureAccessIsCache(NetTable* table) const;
  void ensureAccessIsDirect(NetTable* table) const;

  void pushNewChunkIdsToTrackers();
  friend class ProtoTableFileIO;
  inline void disableChunkTracking() { chunk_tracking_disabled_ = true; }

  // The following function is very dangerous and shouldn't be used apart from
  // where it needs to be used in caches.
  template <typename IdType>
  std::shared_ptr<const Revision>* getMutableUpdateEntry(const IdType& id,
                                                         NetTable* table);
  template <typename IdType>
  friend class NetTableTransactionInterface;

  template <typename IdType, typename ObjectType>
  ThreadsafeCache<IdType, ObjectType>* getMutableCache(NetTable* table);

  void commitImpl(const bool finalize_after_check,
                  std::promise<bool>* will_commit_succeed,
                  CommitFutureTree* future_tree);
  void finalize();

  /**
   * A global ordering of tables prevents deadlocks (resource hierarchy
   * solution)
   */
  struct NetTableOrdering {
    inline bool operator()(const NetTable* a, const NetTable* b) const {
      return CHECK_NOTNULL(a)->name() < CHECK_NOTNULL(b)->name();
    }
  };
  typedef std::map<NetTable*, std::shared_ptr<NetTableTransaction>,
                   NetTableOrdering> TransactionMap;
  typedef TransactionMap::value_type TransactionPair;
  mutable TransactionMap net_table_transactions_;
  std::shared_ptr<Workspace> workspace_;
  LogicalTime begin_time_, commit_time_;

  // direct access vs. caching
  enum class TableAccessMode {
    kDirect,
    kCache
  };
  typedef std::unordered_map<NetTable*, TableAccessMode> TableAccessModeMap;
  /**
   * A table may only be accessed directly through a transaction or through a
   * cache, but not both. Otherwise, getting uncommitted entries becomes rather
   * complicated.
   */
  mutable TableAccessModeMap access_mode_;
  typedef std::unordered_map<NetTable*, std::shared_ptr<CacheBase>> CacheMap;
  CacheMap caches_;
  /**
   * Cache must be able to access transaction directly, even though table
   * is in cache access mode. This on a per-thread basis.
   */
  std::unordered_set<std::thread::id> cache_access_override_;
  mutable std::mutex access_type_mutex_;
  mutable std::mutex access_mode_mutex_;
  mutable std::mutex net_table_transactions_mutex_;

  bool chunk_tracking_disabled_;

  bool is_parallel_commit_running_;
  std::mutex m_is_parallel_commit_running_;
  std::condition_variable cv_is_parallel_commit_running_;

  bool finalized_;
};

}  // namespace map_api

#include "./transaction-inl.h"

#endif  // MAP_API_TRANSACTION_H_
