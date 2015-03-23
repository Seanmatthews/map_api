#ifndef MAP_API_READER_WRITER_MUTEX_FIXTURE_INL_H_
#define MAP_API_READER_WRITER_MUTEX_FIXTURE_INL_H_

#include <gtest/gtest.h>

#include "./reader_writer_mutex_fixture.h"

namespace map_api {

void ReaderWriterMutexFixture::SetUp() {
  ::testing::Test::SetUp();
  value_ = 0;
  num_writes_ = 0;
  num_upgrade_failures_ = 0;
}

void ReaderWriterMutexFixture::reader() {
  for (int i = 0; i < kNumCycles; ++i) {
    map_api::ScopedReadLock lock(&value_mutex_);
    EXPECT_EQ(0, value_ % kMagicNumber);
  }
}

void ReaderWriterMutexFixture::writer() {
  for (int i = 0; i < kNumCycles; ++i) {
    map_api::ScopedWriteLock lock(&value_mutex_);
    value_ = i * kMagicNumber;
  }
}

void ReaderWriterMutexFixture::delayedReader() {
  for (int i = 0; i < kNumCycles; ++i) {
    value_mutex_.acquireReadLock();
    usleep(5);
    EXPECT_EQ(value_, (num_writes_) * kMagicNumber);
    value_mutex_.releaseReadLock();
  }
}

void ReaderWriterMutexFixture::readerUpgrade() {
  for (int i = 0; i < kNumCycles; ++i) {
    value_mutex_.acquireReadLock();
    EXPECT_EQ(0, value_ % kMagicNumber);
    int read_value = value_;
    usleep(5);
    if (value_mutex_.upgradeToWriteLock()) {
      value_ = read_value + kMagicNumber;
      ++(num_writes_);
      value_mutex_.releaseWriteLock();
    } else {
      ++num_upgrade_failures_;
    }
  }
}

}  // namespace map_api

#endif  // MAP_API_READER_WRITER_MUTEX_FIXTURE_INL_H_
