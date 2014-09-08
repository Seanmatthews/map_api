#ifndef MAP_API_NET_TABLE_TRANSACTION_H_
#define MAP_API_NET_TABLE_TRANSACTION_H_

#include <map>
#include <memory>
#include <string>

#include <gtest/gtest_prod.h>

#include "map-api/chunk.h"
#include "map-api/chunk-transaction.h"
#include "map-api/logical-time.h"
#include "map-api/net-table.h"
#include "map-api/revision.h"
#include "map-api/unique-id.h"

namespace map_api {

class NetTableTransaction {
  friend class Transaction;
  friend class NetTableTest;
  FRIEND_TEST(NetTableTest, NetTableTransactions);

 private:
  explicit NetTableTransaction(NetTable* table);
  NetTableTransaction(const LogicalTime& begin_time, NetTable* table);

  // READ (see transaction.h)
  template <typename IdType>
  std::shared_ptr<Revision> getById(const IdType& id);
  template <typename IdType>
  std::shared_ptr<Revision> getById(const IdType& id, Chunk* chunk);
  CRTable::RevisionMap dumpChunk(Chunk* chunk);
  CRTable::RevisionMap dumpActiveChunks();
  template <typename ValueType>
  CRTable::RevisionMap find(const std::string& key, const ValueType& value);
  template <typename IdType>
  void getAvailableIds(std::unordered_set<IdType>* ids);

  // WRITE (see transaction.h)
  void insert(Chunk* chunk, std::shared_ptr<Revision> revision);
  void update(std::shared_ptr<Revision> revision);

  // TRANSACTION OPERATIONS
  /**
   * Equivalent to lock(), if (check()) commit each sub-transaction, unlock()
   * Returns false if check fails.
   */
  bool commit();
  /**
   * Commit with specified time and under the guarantee that the required
   * sub-transactions are locked and checked.
   */
  void checkedCommit(const LogicalTime& time);
  /**
   * Locks each chunk write-affected by this transaction
   */
  void lock();
  void unlock();
  /**
   * Checks all sub-transactions.
   * Returns false if any sub-check fails.
   * lock() MUST have been called
   */
  bool check();
  void merge(const std::shared_ptr<NetTableTransaction>& merge_transaction,
             ChunkTransaction::Conflicts* conflicts);
  size_t numChangedItems() const;

  // INTERNAL
  ChunkTransaction* transactionOf(Chunk* chunk);
  template <typename IdType>
  Chunk* chunkOf(const IdType& id, std::shared_ptr<Revision>* latest);
  /**
   * A global ordering of chunks prevents deadlocks (resource hierarchy
   * solution)
   */
  struct ChunkOrdering {
    inline bool operator()(const Chunk* a, const Chunk* b) const {
      return CHECK_NOTNULL(a)->id() < CHECK_NOTNULL(b)->id();
    }
  };

  typedef std::map<Chunk*, std::shared_ptr<ChunkTransaction>, ChunkOrdering>
  TransactionMap;
  typedef TransactionMap::value_type TransactionPair;
  TransactionMap chunk_transactions_;
  LogicalTime begin_time_;
  NetTable* table_;
};

}  // namespace map_api

#include "map-api/net-table-transaction-inl.h"

#endif  // MAP_API_NET_TABLE_TRANSACTION_H_
