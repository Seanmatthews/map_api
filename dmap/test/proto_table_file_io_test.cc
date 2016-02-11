#include <fstream>  // NOLINT
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "dmap/ipc.h"
#include "dmap/net-table-transaction.h"
#include "dmap/proto-table-file-io.h"
#include "dmap/test/testing-entrypoint.h"
#include "dmap/transaction.h"
#include "./net_table_fixture.h"

using namespace dmap;  // NOLINT

class ProtoTableFileIOTest : public NetTableFixture {};

TEST_F(ProtoTableFileIOTest, SaveAndRestoreFromFile) {
  ChunkBase* chunk = table_->newChunk();
  CHECK_NOTNULL(chunk);
  common::Id chunk_id = chunk->id();
  common::Id item_1_id;
  generateId(&item_1_id);
  common::Id item_2_id;
  generateId(&item_2_id);
  {
    Transaction transaction;
    std::shared_ptr<Revision> to_insert_1 = table_->getTemplate();
    to_insert_1->setId(item_1_id);
    to_insert_1->set(kFieldName, 42);
    std::shared_ptr<Revision> to_insert_2 = table_->getTemplate();
    to_insert_2->setId(item_2_id);
    to_insert_2->set(kFieldName, 21);
    transaction.insert(table_, chunk, to_insert_1);
    transaction.insert(table_, chunk, to_insert_2);
    ASSERT_TRUE(transaction.commit());
    ConstRevisionMap retrieved;
    LogicalTime dumptime = LogicalTime::sample();
    chunk->dumpItems(dumptime, &retrieved);
    ASSERT_EQ(2u, retrieved.size());
    ConstRevisionMap::iterator it = retrieved.find(item_1_id);
    ASSERT_FALSE(it == retrieved.end());
    LogicalTime time_1, time_2;
    int item_1, item_2;
    time_1 = it->second->getInsertTime();
    it->second->get(kFieldName, &item_1);
    it = retrieved.find(item_2_id);
    ASSERT_FALSE(it == retrieved.end());
    time_2 = it->second->getInsertTime();
    it->second->get(kFieldName, &item_2);
    EXPECT_EQ(time_1.serialize(), time_2.serialize());
    EXPECT_EQ(item_1, 42);
    EXPECT_EQ(item_2, 21);
  }

  const std::string test_filename = "./test_dump.table";
  // Drop all existing contents.
  std::fstream file;
  file.open(test_filename, std::fstream::binary | std::fstream::in |
                               std::fstream::out | std::fstream::trunc);

  {
    ProtoTableFileIO file_io(test_filename, table_);
    EXPECT_TRUE(file_io.storeTableContents(LogicalTime::sample()));
  }

  // Reset the state of the database.
  TearDown();
  SetUp();

  {
    ProtoTableFileIO file_io(test_filename, table_);
    ASSERT_TRUE(file_io.restoreTableContents());
  }

  {
    chunk = table_->getChunk(chunk_id);
    ASSERT_FALSE(chunk == nullptr);
    ConstRevisionMap retrieved;
    LogicalTime time_1, time_2;
    int item_1, item_2;
    LogicalTime dumptime = LogicalTime::sample();
    chunk->dumpItems(dumptime, &retrieved);
    ASSERT_EQ(2u, retrieved.size());
    ConstRevisionMap::iterator it = retrieved.find(item_1_id);
    ASSERT_FALSE(it == retrieved.end());
    time_1 = it->second->getInsertTime();
    it->second->get(kFieldName, &item_1);
    EXPECT_EQ(item_1, 42);

    it = retrieved.find(item_2_id);
    ASSERT_FALSE(it == retrieved.end());
    time_2 = it->second->getInsertTime();
    it->second->get(kFieldName, &item_2);
    EXPECT_EQ(item_2, 21);

    EXPECT_EQ(time_1.serialize(), time_2.serialize());
  }
}

DMAP_UNITTEST_ENTRYPOINT
