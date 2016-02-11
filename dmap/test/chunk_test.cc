#include <set>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "dmap/hub.h"
#include "dmap/ipc.h"
#include "dmap/test/testing-entrypoint.h"
#include "./net_table_fixture.h"

namespace dmap {

class ChunkTest : public NetTableFixture {};

TEST_F(ChunkTest, NetInsert) {
  ChunkBase* chunk = table_->newChunk();
  ASSERT_TRUE(chunk);
  insert(42, chunk);
}

TEST_F(ChunkTest, ParticipationRequest) {
  enum SubProcesses {
    ROOT,
    A
  };
  enum Barriers {
    INIT,
    DIE
  };
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    ChunkBase* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);

    IPC::barrier(INIT, 1);

    EXPECT_EQ(1, Hub::instance().peerSize());
    EXPECT_EQ(0, chunk->peerSize());
    EXPECT_EQ(1, chunk->requestParticipation());
    EXPECT_EQ(1, chunk->peerSize());

    IPC::barrier(DIE, 1);
  } else {
    IPC::barrier(INIT, 1);
    IPC::barrier(DIE, 1);
  }
}

TEST_F(ChunkTest, FullJoinTwice) {
  enum SubProcesses {
    ROOT,
    A,
    B
  };
  enum Barriers {
    ROOT_A_INIT,
    A_JOINED_B_INIT,
    B_JOINED,
    DIE
  };
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    ChunkBase* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    insert(42, chunk);

    IPC::barrier(ROOT_A_INIT, 1);

    EXPECT_EQ(1, Hub::instance().peerSize());
    EXPECT_EQ(0, chunk->peerSize());
    EXPECT_EQ(1, chunk->requestParticipation());
    EXPECT_EQ(1, chunk->peerSize());
    launchSubprocess(B);

    IPC::barrier(A_JOINED_B_INIT, 2);

    EXPECT_EQ(2, Hub::instance().peerSize());
    EXPECT_EQ(1, chunk->peerSize());
    EXPECT_EQ(1, chunk->requestParticipation());
    EXPECT_EQ(2, chunk->peerSize());

    IPC::barrier(B_JOINED, 2);

    IPC::barrier(DIE, 2);
  }
  if (getSubprocessId() == A) {
    IPC::barrier(ROOT_A_INIT, 1);
    IPC::barrier(A_JOINED_B_INIT, 2);
    EXPECT_EQ(1u, count());
    IPC::barrier(B_JOINED, 2);
    IPC::barrier(DIE, 2);
  }
  if (getSubprocessId() == B) {
    IPC::barrier(A_JOINED_B_INIT, 2);
    IPC::barrier(B_JOINED, 2);
    EXPECT_EQ(1u, count());
    IPC::barrier(DIE, 2);
  }
}

TEST_F(ChunkTest, RemoteInsert) {
  enum Subprocesses {
    ROOT,
    A
  };
  enum Barriers {
    INIT,
    A_JOINED,
    A_ADDED,
    DIE
  };
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    ChunkBase* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    IPC::barrier(INIT, 1);

    chunk->requestParticipation();
    IPC::push(chunk->id().hexString());
    IPC::barrier(A_JOINED, 1);
    IPC::barrier(A_ADDED, 1);

    EXPECT_EQ(1u, count());
    IPC::barrier(DIE, 1);
  }
  if (getSubprocessId() == A) {
    IPC::barrier(INIT, 1);
    IPC::barrier(A_JOINED, 1);
    common::Id chunk_id = IPC::pop<common::Id>();
    insert(42, table_->getChunk(chunk_id));

    IPC::barrier(A_ADDED, 1);
    IPC::barrier(DIE, 1);
  }
}

TEST_F(ChunkTest, Leave) {
  enum SubProcesses {
    ROOT,
    A
  };
  enum Barriers {
    INIT,
    CHUNK_SHARED,
    A_LEFT
  };
  common::generateIdFromInt(1, &chunk_id_);
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    chunk_ = table_->newChunk(chunk_id_);
    insert(42, chunk_);
    IPC::barrier(INIT, 1);

    ASSERT_EQ(1, chunk_->requestParticipation());
    EXPECT_EQ(1, chunk_->peerSize());
    IPC::barrier(CHUNK_SHARED, 1);

    IPC::barrier(A_LEFT, 1);
    EXPECT_EQ(0, chunk_->peerSize());
  }
  if (getSubprocessId() == A) {
    IPC::barrier(INIT, 1);
    IPC::barrier(CHUNK_SHARED, 1);

    chunk_ = table_->getChunk(chunk_id_);
    EXPECT_EQ(1u, table_->numItems());
    table_->leaveAllChunks();
    EXPECT_EQ(0u, table_->numItems());
    IPC::barrier(A_LEFT, 1);
  }
}

TEST_F(ChunkTest, RemoteUpdate) {
  enum Subprocesses {
    ROOT,
    A
  };
  enum Barriers {
    INIT,
    A_JOINED,
    A_UPDATED,
    DIE
  };
  ConstRevisionMap results;
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    ChunkBase* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    insert(42, chunk);
    table_->dumpActiveChunksAtCurrentTime(&results);
    EXPECT_EQ(1u, results.size());
    EXPECT_TRUE(results.begin()->second->verifyEqual(kFieldName, 42));
    IPC::barrier(INIT, 1);

    chunk->requestParticipation();
    IPC::barrier(A_JOINED, 1);
    IPC::barrier(A_UPDATED, 1);
    table_->dumpActiveChunksAtCurrentTime(&results);
    EXPECT_EQ(1u, results.size());
    EXPECT_TRUE(results.begin()->second->verifyEqual(kFieldName, 21));

    IPC::barrier(DIE, 1);
  }
  if (getSubprocessId() == A) {
    IPC::barrier(INIT, 1);
    IPC::barrier(A_JOINED, 1);
    table_->dumpActiveChunksAtCurrentTime(&results);
    EXPECT_EQ(1u, results.size());
    std::shared_ptr<Revision> revision;
    results.begin()->second->copyForWrite(&revision);
    revision->set(kFieldName, 21);
    EXPECT_TRUE(table_->update(revision));

    IPC::barrier(A_UPDATED, 1);
    IPC::barrier(DIE, 1);
  }
}

DEFINE_uint64(grind_processes, 10u,
              "Total amount of processes in ChunkTest.Grind");
DEFINE_uint64(grind_cycles, 10u,
              "Total amount of insert-update cycles in ChunkTest.Grind");

TEST_F(ChunkTest, Grind) {
  const int kInsertUpdateCycles = FLAGS_grind_cycles;
  const uint64_t kProcesses = FLAGS_grind_processes;
  enum Barriers {
    INIT,
    ID_SHARED,
    DIE
  };
  ConstRevisionMap results;
  if (getSubprocessId() == 0) {
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i);
    }
    ChunkBase* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    IPC::barrier(INIT, kProcesses - 1);
    chunk->requestParticipation();
    IPC::push(chunk->id());
    IPC::barrier(ID_SHARED, kProcesses - 1);
    IPC::barrier(DIE, kProcesses - 1);
    EXPECT_EQ(kInsertUpdateCycles * (kProcesses - 1), count());
  } else {
    IPC::barrier(INIT, kProcesses - 1);
    IPC::barrier(ID_SHARED, kProcesses - 1);
    common::Id chunk_id = IPC::pop<common::Id>();
    ChunkBase* chunk = table_->getChunk(chunk_id);
    for (int i = 0; i < kInsertUpdateCycles; ++i) {
      // insert
      insert(42, chunk);
      // update
      table_->dumpActiveChunksAtCurrentTime(&results);
      std::shared_ptr<Revision> revision;
      results.begin()->second->copyForWrite(&revision);
      revision->set(kFieldName, 21);
      EXPECT_TRUE(table_->update(revision));
    }
    IPC::barrier(DIE, kProcesses - 1);
    VLOG(3) << "Finishing...";
  }
}

TEST_F(ChunkTest, ChunkTransactions) {
  const uint64_t kProcesses = FLAGS_grind_processes;
  enum Barriers {
    INIT,
    IDS_SHARED,
    DIE
  };
  ConstRevisionMap results;
  if (getSubprocessId() == 0) {
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i);
    }
    ChunkBase* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    common::Id insert_id = insert(1, chunk);
    IPC::barrier(INIT, kProcesses - 1);

    chunk->requestParticipation();
    IPC::push(chunk->id());
    IPC::push(insert_id);
    IPC::barrier(IDS_SHARED, kProcesses - 1);

    IPC::barrier(DIE, kProcesses - 1);
    table_->dumpActiveChunksAtCurrentTime(&results);
    EXPECT_EQ(kProcesses, results.size());
    std::unordered_map<common::Id, std::shared_ptr<const Revision> >::iterator
        found = results.find(insert_id);
    if (found != results.end()) {
      int final_value;
      found->second->get(kFieldName, &final_value);
      EXPECT_EQ(static_cast<int>(kProcesses), final_value);
    } else {
      // still need a clean disconnect
      EXPECT_TRUE(false);
    }
  } else {
    IPC::barrier(INIT, kProcesses - 1);
    IPC::barrier(IDS_SHARED, kProcesses - 1);
    common::Id chunk_id = IPC::pop<common::Id>();
    common::Id item_id = IPC::pop<common::Id>();
    ChunkBase* chunk = table_->getChunk(chunk_id);
    ASSERT_TRUE(chunk);
    while (true) {
      ChunkTransaction transaction(chunk, table_);
      // insert
      insert(42, &transaction);
      // update
      int transient_value;
      std::shared_ptr<const Revision> to_update = transaction.getById(item_id);
      to_update->get(kFieldName, &transient_value);
      ++transient_value;
      std::shared_ptr<Revision> revision;
      to_update->copyForWrite(&revision);
      revision->set(kFieldName, transient_value);
      transaction.update(revision);
      if (transaction.commit()) {
        break;
      }
    }
    IPC::barrier(DIE, kProcesses - 1);
  }
}

TEST_F(ChunkTest, ChunkTransactionsConflictConditions) {
  const uint64_t kProcesses = FLAGS_grind_processes;
  const int kUniqueItems = 10;
  enum Barriers {
    INIT,
    ID_SHARED,
    DIE
  };
  ConstRevisionMap results;
  if (getSubprocessId() == 0) {
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i);
    }
    ChunkBase* chunk = table_->newChunk();
    ASSERT_TRUE(chunk);
    IPC::barrier(INIT, kProcesses - 1);

    chunk->requestParticipation();
    IPC::push(chunk->id());
    IPC::barrier(ID_SHARED, kProcesses - 1);

    IPC::barrier(DIE, kProcesses - 1);
    table_->dumpActiveChunksAtCurrentTime(&results);
    EXPECT_EQ(kUniqueItems, static_cast<int>(results.size()));
    std::set<int> unique_results;
    for (const ConstRevisionMap::value_type& item : results) {
      int result;
      item.second->get(kFieldName, &result);
      unique_results.insert(result);
    }
    EXPECT_EQ(kUniqueItems, static_cast<int>(unique_results.size()));
    int i = 0;
    for (int unique_result : unique_results) {
      EXPECT_EQ(i, unique_result);
      ++i;
    }
  } else {
    IPC::barrier(INIT, kProcesses - 1);
    IPC::barrier(ID_SHARED, kProcesses - 1);
    common::Id chunk_id = IPC::pop<common::Id>();
    ChunkBase* chunk = table_->getChunk(chunk_id);
    ASSERT_TRUE(chunk);
    for (int i = 0; i < kUniqueItems; ++i) {
      ChunkTransaction transaction(chunk, table_);
      insert(i, &transaction);
      transaction.addConflictCondition(kFieldName, i);
      transaction.commit();
    }
    IPC::barrier(DIE, kProcesses - 1);
  }
}

TEST_F(ChunkTest, Triggers) {
  enum Processes {
    ROOT,
    A
  };
  enum Barriers {
    INIT,
    ID_SHARED,
    TRIGGER_READY,
    DONE,
    DIE
  };
  int highest_value;
  size_t trigger_counter = 0u;
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    IPC::barrier(INIT, 1);
    chunk_ = table_->newChunk();
    chunk_id_ = chunk_->id();
    IPC::push(chunk_id_);
    IPC::barrier(ID_SHARED, 1);
  }
  if (getSubprocessId() == A) {
    IPC::barrier(INIT, 1);
    IPC::barrier(ID_SHARED, 1);
    chunk_id_ = IPC::pop<common::Id>();
    chunk_ = table_->getChunk(chunk_id_);
  }
  EXPECT_EQ(0u, chunk_->attachTrigger([this, &highest_value](
                    const std::unordered_set<common::Id>& insertions,
                    const std::unordered_set<common::Id>& updates) {
                  common::Id id;
                  if (insertions.empty()) {
                    if (updates.empty()) {
                      return;
                    } else {
                      CHECK_EQ(updates.size(), 1u);
                      id = *updates.begin();
                    }
                  } else {
                    CHECK_EQ(insertions.size(), 1u);
                    CHECK(updates.empty());
                    id = *insertions.begin();
                  }
                  Transaction transaction;
                  std::shared_ptr<Revision> item;
                  transaction.getById(id, table_, chunk_)->copyForWrite(&item);
                  item->get(kFieldName, &highest_value);
                  if (highest_value < 10) {
                    ++highest_value;
                    item->set(kFieldName, highest_value);
                    transaction.update(table_, item);
                    CHECK(transaction.commit());
                  }
                }));
  EXPECT_EQ(1u, chunk_->attachTrigger([this, &trigger_counter](
                    const std::unordered_set<common::Id>& insertions,
                    const std::unordered_set<common::Id>& updates) {
                  // Ignore chunk management related unlocks.
                  if (insertions.size() + updates.size() > 0u) {
                    ++trigger_counter;
                  }
                }));
  IPC::barrier(TRIGGER_READY, 1);
  if (getSubprocessId() == ROOT) {
    Transaction transaction;
    std::shared_ptr<Revision> item = table_->getTemplate();
    common::Id insert_id;
    generateId(&insert_id);
    item->setId(insert_id);
    item->set(kFieldName, 0);
    transaction.insert(table_, chunk_, item);
    CHECK(transaction.commit());
    usleep(5e5);  // should suffice for the triggers to do their magic
    IPC::barrier(DONE, 1);
    // These values must be verified before DIE in order to not catch the
    // trigger from Chunk::leave() related unlocks.
    EXPECT_EQ(10, highest_value);
    EXPECT_EQ(5u, trigger_counter);
    IPC::barrier(DIE, 1);
  }
  if (getSubprocessId() == A) {
    IPC::barrier(DONE, 1);
    // These values must be verified before DIE in order to not catch the
    // trigger from Chunk::leave() related unlocks.
    EXPECT_EQ(6u, trigger_counter);
    IPC::barrier(DIE, 1);
  }
}

TEST_F(ChunkTest, SendHistory) {
  enum Processes {
    ROOT,
    A
  };
  enum Barriers {
    INIT,
    A_DONE,
    DIE
  };
  LogicalTime before_mod;
  constexpr int kBefore = 42, kAfter = 21;
  if (getSubprocessId() == ROOT) {
    launchSubprocess(A);
    IPC::barrier(INIT, 1);
    IPC::barrier(A_DONE, 1);
    chunk_id_ = IPC::pop<common::Id>();
    before_mod = IPC::pop<LogicalTime>();
    item_id_ = IPC::pop<common::Id>();
    chunk_ = table_->getChunk(chunk_id_);
    IPC::barrier(DIE, 1);

    Transaction current_transaction;
    std::shared_ptr<const Revision> current_version =
        current_transaction.getById(item_id_, table_, chunk_);
    ASSERT_TRUE(current_version.get() != nullptr);
    EXPECT_TRUE(current_version->verifyEqual(kFieldName, kAfter));

    Transaction time_travel(before_mod);
    std::shared_ptr<const Revision> past_version =
        time_travel.getById(item_id_, table_, chunk_);
    ASSERT_TRUE(past_version.get() != nullptr);
    EXPECT_TRUE(past_version->verifyEqual(kFieldName, kBefore));
  }
  if (getSubprocessId() == A) {
    IPC::barrier(INIT, 1);
    chunk_ = table_->newChunk();
    IPC::push(chunk_->id());
    Transaction insert_transaction;
    insert(kBefore, &item_id_, &insert_transaction);
    CHECK(insert_transaction.commit());
    IPC::push(LogicalTime::sample());
    Transaction update_transaction;
    std::shared_ptr<Revision> to_update;
    update_transaction.getById(item_id_, table_, chunk_)
        ->copyForWrite(&to_update);
    to_update->set(kFieldName, kAfter);
    update_transaction.update(table_, to_update);
    CHECK(update_transaction.commit());
    IPC::push(item_id_);
    IPC::barrier(A_DONE, 1);
    IPC::barrier(DIE, 1);
  }
}

TEST_F(ChunkTest, GetCommitTimes) {
  chunk_ = table_->newChunk();
  Transaction first;
  common::Id id;
  insert(42, &id, &first);
  ASSERT_TRUE(first.commit());
  Transaction second;
  update(21, id, &second);
  insert(42, &id, &second);
  ASSERT_TRUE(second.commit());
  std::set<LogicalTime> commit_times;
  chunk_->getCommitTimes(LogicalTime::sample(), &commit_times);
  EXPECT_EQ(2u, commit_times.size());
  EXPECT_TRUE(commit_times.find(first.getCommitTime()) != commit_times.end());
  EXPECT_TRUE(commit_times.find(second.getCommitTime()) != commit_times.end());
}

}  // namespace dmap

MAP_API_UNITTEST_ENTRYPOINT
