#include <string>
#include <type_traits>

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <map-api-common/unique-id.h>

#include "map-api/core.h"
#include "map-api/legacy-chunk-data-ram-container.h"
#include "map-api/legacy-chunk-data-stxxl-container.h"
#include "map-api/logical-time.h"
#include "map-api/test/testing-entrypoint.h"
#include "./test_table.cc"

namespace map_api {

template <typename TableType>
class TableDataContainerTest : public ::testing::Test {
 public:
  virtual void SetUp() final override {
    Core::initializeInstance();
    ASSERT_TRUE(Core::instance() != nullptr);
  }
  virtual void TearDown() final override { Core::instance()->kill(); }
};

typedef ::testing::Types<LegacyChunkDataRamContainer,
                         LegacyChunkDataStxxlContainer> TableTypes;
TYPED_TEST_CASE(TableDataContainerTest, TableTypes);

TYPED_TEST(TableDataContainerTest, initEmpty) {
  TestTable<TypeParam>::instance();
  std::shared_ptr<Revision> structure =
      TestTable<TypeParam>::instance().getTemplate();
  ASSERT_TRUE(static_cast<bool>(structure));
  EXPECT_EQ(0, structure->customFieldCount());
}

/**
 **********************************************************
 * TEMPLATED TABLE WITH A SINGLE FIELD OF THE TEMPLATE TYPE
 **********************************************************
 */

template <typename _TableType, typename _DataType>
class TableDataTypes {
 public:
  typedef _TableType TableType;
  typedef _DataType DataType;
};

template <typename TableDataType>
class FieldTestTable : public TestTable<typename TableDataType::TableType> {
 public:
  enum Fields {
    kTestField
  };
  static typename TableDataType::TableType* forge() {
    typename TableDataType::TableType* table =
        new typename TableDataType::TableType;
    std::shared_ptr<map_api::TableDescriptor> descriptor(
        new map_api::TableDescriptor);
    descriptor->setName("field_test_table");
    descriptor->template addField<typename TableDataType::DataType>(kTestField);
    table->init(descriptor);
    return table;
  }
};

/**
 **************************************
 * FIXTURES FOR TYPED TABLE FIELD TESTS
 **************************************
 */
template <typename TestedType>
class FieldTest : public ::testing::Test {
 protected:
  /**
   * Sample data for tests. MUST BE NON-DEFAULT!
   */
  TestedType sample_data_1();
  TestedType sample_data_2();
};

template <>
class FieldTest<std::string> : public ::testing::Test {
 protected:
  std::string sample_data_1() { return "Test_string_1"; }
  std::string sample_data_2() { return "Test_string_2"; }
};
template <>
class FieldTest<double> : public ::testing::Test {
 protected:
  double sample_data_1() { return 3.14; }
  double sample_data_2() { return -3.14; }
};
template <>
class FieldTest<int32_t> : public ::testing::Test {
 protected:
  int32_t sample_data_1() { return 42; }
  int32_t sample_data_2() { return -42; }
};
template <>
class FieldTest<map_api_common::Id> : public ::testing::Test {
 protected:
  map_api_common::Id sample_data_1() {
    map_api_common::Id id;
    id.fromHexString("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    return id;
  }
  map_api_common::Id sample_data_2() {
    map_api_common::Id id;
    id.fromHexString("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    return id;
  }
};
template <>
class FieldTest<int64_t> : public ::testing::Test {
 protected:
  int64_t sample_data_1() { return 9223372036854775807; }
  int64_t sample_data_2() { return -9223372036854775807; }
};
template <>
class FieldTest<map_api::LogicalTime> : public ::testing::Test {
 protected:
  LogicalTime sample_data_1() { return LogicalTime(9223372036854775807u); }
  LogicalTime sample_data_2() { return LogicalTime(9223372036854775u); }
};
template <>
class FieldTest<testBlob> : public ::testing::Test {
 protected:
  testBlob sample_data_1() {
    testBlob field;
    field.set_type(map_api::proto::Type::DOUBLE);
    field.set_double_value(3);
    return field;
  }
  testBlob sample_data_2() {
    testBlob field;
    field.set_type(map_api::proto::Type::INT32);
    field.set_int_value(42);
    return field;
  }
};

template <typename TableDataType>
class FieldTestWithoutInit
    : public FieldTest<typename TableDataType::DataType> {
 public:
  virtual ~FieldTestWithoutInit() {}

  virtual void SetUp() override {
    FieldTest<typename TableDataType::DataType>::SetUp();
    table_.reset(new typename TableDataType::TableType());
  }

  virtual void TearDown() override {
    FieldTest<typename TableDataType::DataType>::TearDown();
  }

 protected:
  typedef FieldTestTable<TableDataType> FieldTestTableType;
  inline const typename FieldTestTableType::Fields fieldName() {
    return FieldTestTableType::kTestField;
  }
  std::shared_ptr<Revision> getTemplate() {
    query_ = this->table_->getTemplate();
    return query_;
  }

  map_api_common::Id fillRevision(const typename TableDataType::DataType& value) {
    getTemplate();
    map_api_common::Id inserted;
    generateId(&inserted);
    query_->setId(inserted);
    // to_insert_->set("owner", Id::random()); TODO(tcies) later, from core
    query_->set(FieldTestTableType::kTestField, value);
    return inserted;
  }

  map_api_common::Id fillRevision() { return fillRevision(this->sample_data_1()); }

  bool insertRevision() {
    return this->table_->insert(LogicalTime::sample(), query_);
  }

  void getRevision(const map_api_common::Id& id) {
    this->table_->getById(id, LogicalTime::sample())->copyForWrite(&query_);
  }

  std::unique_ptr<typename TableDataType::TableType> table_;
  std::shared_ptr<Revision> query_;
};

template <typename TableDataType>
class FieldTestWithInit : public FieldTestWithoutInit<TableDataType> {
 public:
  virtual ~FieldTestWithInit() {}

 protected:
  virtual void SetUp() override {
    Core::initializeInstance();
    ASSERT_TRUE(Core::instance() != nullptr);
    this->table_.reset(FieldTestTable<TableDataType>::forge());
  }
  virtual void TearDown() override { Core::instance()->kill(); }
};

template <typename TableDataType>
class UpdateFieldTestWithInit : public FieldTestWithInit<TableDataType> {
 protected:
  bool updateRevision() {
    std::shared_ptr<Revision> revision;
    this->query_->copyForWrite(&revision);
    this->table_->update(LogicalTime::sample(), revision);
    return true;
  }

  void fillRevisionWithOtherData() {
    this->query_->set(this->fieldName(), this->sample_data_2());
  }
};

template <typename TableType>
class IntTestWithInit
    : public FieldTestWithInit<TableDataTypes<TableType, int64_t>> {
};  // NOLINT

template <typename TableType>
class CruMapIntTestWithInit
    : public UpdateFieldTestWithInit<
          TableDataTypes<LegacyChunkDataRamContainer, int64_t>> {};  // NOLINT

/**
 *************************
 * TYPED TABLE FIELD TESTS
 *************************
 */

#define ALL_DATA_TYPES(table_type)                                             \
  TableDataTypes<table_type, testBlob>,                                        \
      TableDataTypes<table_type, std::string>,                                 \
      TableDataTypes<table_type, int32_t>, TableDataTypes<table_type, double>, \
      TableDataTypes<table_type, map_api_common::Id>,                                  \
      TableDataTypes<table_type, int64_t>,                                     \
      TableDataTypes<table_type, map_api::LogicalTime>

typedef ::testing::Types<ALL_DATA_TYPES(LegacyChunkDataRamContainer),
                         ALL_DATA_TYPES(LegacyChunkDataStxxlContainer)>
    AllTypes;

TYPED_TEST_CASE(FieldTestWithoutInit, AllTypes);
TYPED_TEST_CASE(FieldTestWithInit, AllTypes);
TYPED_TEST_CASE(UpdateFieldTestWithInit, AllTypes);
TYPED_TEST_CASE(IntTestWithInit, TableTypes);
TYPED_TEST_CASE(CruMapIntTestWithInit, AllTypes);

TYPED_TEST(FieldTestWithInit, Init) {
  EXPECT_EQ(1, this->getTemplate()->customFieldCount());
}

TYPED_TEST(FieldTestWithoutInit, CreateBeforeInit) {
  ::testing::FLAGS_gtest_death_test_style = "fast";
  EXPECT_DEATH(this->fillRevision(),
               "Can't get template of non-initialized table");
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
}

TYPED_TEST(FieldTestWithoutInit, ReadBeforeInit) {
  ::testing::FLAGS_gtest_death_test_style = "fast";
  map_api_common::Id item_id;
  generateId(&item_id);
  EXPECT_DEATH(this->table_->getById(item_id, LogicalTime::sample()),
               "Attempted to getById from non-initialized table");
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
}

TYPED_TEST(FieldTestWithInit, CreateRead) {
  map_api_common::Id inserted = this->fillRevision();
  EXPECT_TRUE(this->insertRevision());

  std::shared_ptr<Revision> rowFromTable;
  this->table_->getById(inserted, LogicalTime::sample())
      ->copyForWrite(&rowFromTable);
  ASSERT_TRUE(static_cast<bool>(rowFromTable));
  typename TypeParam::DataType dataFromTable;
  rowFromTable->get(FieldTestTable<TypeParam>::kTestField, &dataFromTable);
  EXPECT_EQ(this->sample_data_1(), dataFromTable);
}

TYPED_TEST(FieldTestWithInit, ReadInexistentRow) {
  this->fillRevision();
  EXPECT_TRUE(this->insertRevision());

  map_api_common::Id other_id;
  generateId(&other_id);
  EXPECT_FALSE(this->table_->getById(other_id, LogicalTime::sample()));
}

TYPED_TEST(FieldTestWithInit, ReadInexistentRowData) {
  map_api_common::Id inserted = this->fillRevision();
  EXPECT_TRUE(this->insertRevision());

  std::shared_ptr<Revision> rowFromTable;
  this->table_->getById(inserted, LogicalTime::sample())
      ->copyForWrite(&rowFromTable);
  EXPECT_TRUE(static_cast<bool>(rowFromTable));
  typename TypeParam::DataType dataFromTable;
  ::testing::FLAGS_gtest_death_test_style = "fast";
  EXPECT_DEATH(rowFromTable->get(13, &dataFromTable),
               "Index out of custom field bounds");
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
}

TYPED_TEST(UpdateFieldTestWithInit, UpdateRead) {
  map_api_common::Id inserted = this->fillRevision();
  EXPECT_TRUE(this->insertRevision());

  std::shared_ptr<const Revision> rowFromTable =
      this->table_->getById(inserted, LogicalTime::sample());
  ASSERT_TRUE(static_cast<bool>(rowFromTable));
  typename TypeParam::DataType dataFromTable;
  rowFromTable->get(this->fieldName(), &dataFromTable);
  EXPECT_EQ(this->sample_data_1(), dataFromTable);

  this->fillRevisionWithOtherData();
  this->updateRevision();
  rowFromTable = this->table_->getById(inserted, LogicalTime::sample());
  EXPECT_TRUE(static_cast<bool>(rowFromTable));
  rowFromTable->get(this->fieldName(), &dataFromTable);
  EXPECT_EQ(this->sample_data_2(), dataFromTable);
}

TYPED_TEST(IntTestWithInit, CreateReadThousand) {
  for (int i = 0; i < 1000; ++i) {
    map_api_common::Id inserted = this->fillRevision(i);
    EXPECT_TRUE(this->insertRevision());

    std::shared_ptr<const Revision> rowFromTable =
        this->table_->getById(inserted, LogicalTime::sample());
    ASSERT_TRUE(static_cast<bool>(rowFromTable));
    int64_t dataFromTable;
    rowFromTable->get(
        FieldTestTable<TableDataTypes<TypeParam, int64_t>>::kTestField,
        &dataFromTable);
    EXPECT_EQ(i, dataFromTable);
  }
}

TYPED_TEST(CruMapIntTestWithInit, HistoryAtTime) {
  typedef FieldTestTable<TypeParam> FieldTestTableType;
  constexpr int64_t kFirst = 42, kSecond = 21, kThird = 84;
  map_api_common::Id id = this->fillRevision(kFirst);
  ASSERT_TRUE(this->insertRevision());
  this->getRevision(id);
  ASSERT_TRUE(this->query_.get() != nullptr);
  this->query_->set(FieldTestTableType::kTestField, kSecond);
  ASSERT_TRUE(this->updateRevision());
  LogicalTime before_third = LogicalTime::sample();
  this->getRevision(id);
  ASSERT_TRUE(this->query_.get() != nullptr);
  this->query_->set(FieldTestTableType::kTestField, kThird);
  ASSERT_TRUE(this->updateRevision());

  LegacyChunkDataContainerBase::History old_history;
  this->table_->itemHistory(id, before_third, &old_history);
  EXPECT_EQ(2u, old_history.size());

  LegacyChunkDataContainerBase::History new_history;
  this->table_->itemHistory(id, LogicalTime::sample(), &new_history);
  EXPECT_EQ(3u, new_history.size());
}

TYPED_TEST(CruMapIntTestWithInit, Remove) {
  constexpr int64_t kValue = 42;
  this->fillRevision(kValue);
  this->insertRevision();

  EXPECT_EQ(1, this->table_->count(-1, 0, LogicalTime::sample()));
  std::vector<map_api_common::Id> ids;
  this->table_->getAvailableIds(LogicalTime::sample(), &ids);
  EXPECT_EQ(1u, ids.size());
  ConstRevisionMap result;
  this->table_->find(-1, 0, LogicalTime::sample(), &result);
  EXPECT_EQ(1u, result.size());

  std::shared_ptr<Revision> revision;
  result.begin()->second->copyForWrite(&revision);
  this->table_->remove(LogicalTime::sample(), revision);

  EXPECT_EQ(0, this->table_->count(-1, 0, LogicalTime::sample()));
  this->table_->getAvailableIds(LogicalTime::sample(), &ids);
  EXPECT_EQ(0u, ids.size());
  this->table_->find(-1, 0, LogicalTime::sample(), &result);
  EXPECT_EQ(0u, result.size());
}

}  // namespace map_api

DMAP_UNITTEST_ENTRYPOINT
