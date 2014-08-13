#include "map-api/map-api-core.h"
#include "map-api/net-table.h"
#include "map-api/net-table-transaction.h"
#include "map-api/transaction.h"

#include "map_api_multiprocess_fixture.h"

using namespace map_api;

class NetTableTest : public MultiprocessTest,
public ::testing::WithParamInterface<bool> {
 protected:
  virtual void SetUp() {
    MultiprocessTest::SetUp();
    std::unique_ptr<TableDescriptor> descriptor(new TableDescriptor);
    descriptor->setName(kTableName);
    descriptor->addField<int>(kFieldName);
    NetTableManager::instance().addTable(
        GetParam() ? CRTable::Type::CRU : CRTable::Type::CR, &descriptor);
    table_ = &NetTableManager::instance().getTable(kTableName);
    table_ = &NetTableManager::instance().getTable(kTableName);
  }

  size_t count() {
    std::unordered_map<Id, std::shared_ptr<Revision> > results;
    table_->dumpActiveChunksAtCurrentTime(&results);
    return results.size();
  }

  void increment(const Id& id, Chunk* chunk,
                 NetTableTransaction* transaction) {
    CHECK_NOTNULL(chunk);
    CHECK_NOTNULL(transaction);
    CRTable::RevisionMap chunk_dump;
    chunk->dumpItems(transaction->time(), &chunk_dump);
    CRTable::RevisionMap::iterator found = chunk_dump.find(id);
    std::shared_ptr<Revision> to_update = found->second;
    int transient_value;
    to_update->get(kFieldName, &transient_value);
    ++transient_value;
    to_update->set(kFieldName, transient_value);
    transaction->update(to_update);
  }

  // TODO(tcies) could replace chunk with chunk_id
  void increment(NetTable* table, const Id& id, Chunk* chunk,
                 Transaction* transaction) {
    CHECK_NOTNULL(table);
    CHECK_NOTNULL(chunk);
    CHECK_NOTNULL(transaction);
    CRTable::RevisionMap chunk_dump;
    chunk->dumpItems(transaction->time(), &chunk_dump);
    CRTable::RevisionMap::iterator found = chunk_dump.find(id);
    std::shared_ptr<Revision> to_update = found->second;
    int transient_value;
    to_update->get(kFieldName, &transient_value);
    ++transient_value;
    to_update->set(kFieldName, transient_value);
    transaction->update(table, to_update);
  }

  Id insert(int n, Chunk* chunk) {
    Id insert_id = Id::generate();
    std::shared_ptr<Revision> to_insert = table_->getTemplate();
    to_insert->set(CRTable::kIdField, insert_id);
    to_insert->set(kFieldName, n);
    EXPECT_TRUE(table_->insert(chunk, to_insert.get()));
    return insert_id;
  }

  Id insert(int n, ChunkTransaction* transaction) {
    Id insert_id = Id::generate();
    std::shared_ptr<Revision> to_insert = table_->getTemplate();
    to_insert->set(CRTable::kIdField, insert_id);
    to_insert->set(kFieldName, n);
    transaction->insert(to_insert);
    return insert_id;
  }

  const std::string kTableName = "chunk_test_table";
  const std::string kFieldName = "chunk_test_field";
  NetTable* table_;
};

INSTANTIATE_TEST_CASE_P(Default, NetTableTest, ::testing::Values(false, true));
