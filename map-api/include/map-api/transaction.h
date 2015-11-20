#ifndef MAP_API_TRANSACTION_H_
#define MAP_API_TRANSACTION_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glog/logging.h>

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
  inline LogicalTime getCommitTime() const { return commit_time_; }
  /**
   * Merge_transaction will be filled with all insertions and non-conflicting
   * updates from this transaction, while the conflicting updates will be
   * represented in a ConflictMap.
   */
  void merge(const std::shared_ptr<Transaction>& merge_transaction,
             ConflictMap* conflicts);

  // ==========
  // STATISTICS
  // ==========
  size_t numChangedItems() const;

  // =============
  // MISCELLANEOUS
  // =============
  template <typename TrackerIdType>
  void overrideTrackerIdentificationMethod(
      NetTable* trackee_table, NetTable* tracker_table,
      const std::function<TrackerIdType(const Revision&)>&
          how_to_determine_tracker);
  template <typename IdType, typename ObjectType>
  std::shared_ptr<ThreadsafeCache<IdType, ObjectType>> createCache(
      NetTable* table);
  template <typename IdType, typename ObjectType>
  const ThreadsafeCache<IdType, ObjectType>& getCache(NetTable* table);
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
};

}  // namespace map_api

#include "./transaction-inl.h"

#endif  // MAP_API_TRANSACTION_H_
