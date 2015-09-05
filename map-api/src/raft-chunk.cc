#include <map-api/raft-chunk.h>

#include <multiagent-mapping-common/conversions.h>

#include "./core.pb.h"
#include "./chunk.pb.h"
#include "map-api/raft-chunk-data-ram-container.h"
#include "map-api/raft-node.h"
#include "map-api/hub.h"
#include "map-api/message.h"

namespace map_api {

RaftChunk::RaftChunk()
    : chunk_lock_attempted_(false),
      is_raft_chunk_lock_acquired_(false),
      lock_log_index_(0),
      chunk_write_lock_depth_(0),
      leave_requested_(false) {}

RaftChunk::~RaftChunk() {
  raft_node_.stop();
  raft_node_.data_ = NULL;
}

bool RaftChunk::init(const common::Id& id,
                     std::shared_ptr<TableDescriptor> descriptor,
                     bool initialize) {
  id_ = id;
  // TODO(aqurai): init new data container here.
  data_container_.reset(raft_node_.data_);
  CHECK(data_container_->init(descriptor));
  raft_node_.chunk_id_ = id_;
  raft_node_.table_name_ = descriptor->name();
  raft_node_.initializeMultiChunkTransactionManager();
  initialized_ = true;
  return true;
}

void RaftChunk::initializeNewImpl(
    const common::Id& id, const std::shared_ptr<TableDescriptor>& descriptor) {
  CHECK(init(id, descriptor, true));

  VLOG(1) << " INIT chunk at peer " << PeerId::self() << " in table "
          << raft_node_.table_name_;
  setStateLeaderAndStartRaft();
}

bool RaftChunk::init(const common::Id& id,
                     const proto::InitRequest& init_request,
                     std::shared_ptr<TableDescriptor> descriptor) {
  CHECK(init(id, descriptor, true));

  VLOG(2) << " INIT chunk at peer " << PeerId::self() << " in table "
          << raft_node_.table_name_;
  raft_node_.initChunkData(init_request);
  setStateFollowerAndStartRaft();
  return true;
}

void RaftChunk::dumpItems(const LogicalTime& time, ConstRevisionMap* items) const {
  CHECK_NOTNULL(items);
  data_container_->dump(time, items);
}

size_t RaftChunk::numItems(const LogicalTime& time) const {
  return data_container_->numAvailableIds(time);
}

size_t RaftChunk::itemsSizeBytes(const LogicalTime& time) const {
  ConstRevisionMap items;
  data_container_->dump(time, &items);
  size_t num_bytes = 0;
  for (const std::pair<common::Id, std::shared_ptr<const Revision> >& item :
       items) {
    CHECK(item.second != nullptr);
    const Revision& revision = *item.second;
    num_bytes += revision.byteSize();
  }
  return num_bytes;
}

void RaftChunk::getCommitTimes(const LogicalTime& sample_time,
                               std::set<LogicalTime>* commit_times) const {
  CHECK_NOTNULL(commit_times);
  //  Using a temporary unordered map because it should have a faster insertion
  //  time. The expected amount of commit times << the expected amount of items,
  //  so this should be worth it.
  std::unordered_set<LogicalTime> unordered_commit_times;
  ConstRevisionMap items;
  RaftChunkDataRamContainer::HistoryMap histories;
  static_cast<RaftChunkDataRamContainer*>(data_container_.get())
      ->chunkHistory(id(), sample_time, &histories);
  for (const RaftChunkDataRamContainer::HistoryMap::value_type& history :
       histories) {
    for (const std::shared_ptr<const Revision>& revision : history.second) {
      unordered_commit_times.insert(revision->getUpdateTime());
    }
  }
  commit_times->insert(unordered_commit_times.begin(),
                       unordered_commit_times.end());
}

bool RaftChunk::insert(const LogicalTime& time,
                       const std::shared_ptr<Revision>& item) {
  CHECK(item != nullptr);
  item->setChunkId(id());
  writeLock();
  static_cast<RaftChunkDataRamContainer*>(data_container_.get())
      ->checkAndPrepareInsert(time, item);
  CHECK(raft_node_.isRunning());
  if (raftInsertRequest(item)) {
    syncLatestCommitTime(*item);
    unlock();
    return true;
  } else {
    unlock();
    return false;
  }
}

void RaftChunk::writeLock() { raftChunkLock(); }

void RaftChunk::readLock() const { raftChunkLock(); }

void RaftChunk::raftChunkLock() const {
  CHECK(raft_node_.isRunning());
  std::lock_guard<std::mutex> lock_mutex(write_lock_mutex_);
  VLOG(3) << PeerId::self() << " Attempting lock for chunk " << id()
          << ". Current depth: " << chunk_write_lock_depth_;
  chunk_lock_attempted_ = true;
  uint64_t serial_id = 0;
  if (is_raft_chunk_lock_acquired_) {
    ++chunk_write_lock_depth_;
  } else {
    CHECK_EQ(lock_log_index_, 0);
    serial_id = request_id_.getNewId();
    while (raft_node_.isRunning()) {
      if (raft_node_.sendChunkLockRequest(serial_id) > 0) {
        break;
      }
      VLOG(3) << PeerId::self() << "Request unsuccessful for locking chunk "
              << id_;
      usleep(150 * kMillisecondsToMicroseconds);
    }

    // Lock is not immediately granted on commit if there is a queue.
    while (raft_node_.isRunning() &&
           !raft_node_.raft_chunk_lock_.isLockHolder(PeerId::self())) {
      VLOG_EVERY_N(2, 50) << PeerId::self()
                          << "Waiting in queue for locking chunk " << id_
                          << ". Current lock holder "
                          << raft_node_.raft_chunk_lock_.holder();
      usleep(20 * kMillisecondsToMicroseconds);
    }
    lock_log_index_ = raft_node_.raft_chunk_lock_.lock_entry_index();
    CHECK(raft_node_.raft_chunk_lock_.isLockHolder(PeerId::self()));

    if (lock_log_index_ > 0) {
      is_raft_chunk_lock_acquired_ = true;
    }
  }
  VLOG(3) << PeerId::self() << " acquired lock for chunk " << id()
          << ". Current depth: " << chunk_write_lock_depth_;
}

bool RaftChunk::isWriteLocked() {
  std::lock_guard<std::mutex> lock(write_lock_mutex_);
  return is_raft_chunk_lock_acquired_;
}

void RaftChunk::unlock() const { unlock(true); }

void RaftChunk::unlock(bool proceed_transaction) const {
  CHECK(raft_node_.isRunning());
  std::lock_guard<std::mutex> lock(write_lock_mutex_);
  VLOG(3) << PeerId::self() << " Attempting unlock for chunk " << id()
          << ". Current depth: " << chunk_write_lock_depth_;
  uint64_t serial_id = 0;
  if (!is_raft_chunk_lock_acquired_) {
    return;
  }
  if (chunk_write_lock_depth_ > 0) {
    --chunk_write_lock_depth_;
  } else if (chunk_write_lock_depth_ == 0) {
    // Send unlock request to leader.
    CHECK(raft_node_.raft_chunk_lock_.isLockHolder(PeerId::self()))
        << " Failed on " << PeerId::self();
    serial_id = request_id_.getNewId();
    while (raft_node_.isRunning()) {
      if (raft_node_.sendChunkUnlockRequest(serial_id, lock_log_index_,
                                            proceed_transaction) > 0) {
        break;
      }
      usleep(500 * kMillisecondsToMicroseconds);
    }
    CHECK(!raft_node_.raft_chunk_lock_.isLockHolder(PeerId::self()));
    lock_log_index_ = 0;
    is_raft_chunk_lock_acquired_ = false;
    chunk_lock_attempted_ = false;
  }
}

int RaftChunk::requestParticipation() {
  std::set<PeerId> peers;
  Hub::instance().getPeers(&peers);
  int num_success = 0;
  for (const PeerId& peer : peers) {
    if (requestParticipation(peer)) {
      ++num_success;
    } else {
      return 0;
    }
  }
  return num_success;
}

const PeerId& RaftChunk::getLockHolder() const {
  return raft_node_.raft_chunk_lock_.holder();
}

int RaftChunk::requestParticipation(const PeerId& peer) {
  if (raft_node_.getState() == RaftNode::State::LEADER &&
      !raft_node_.hasPeer(peer)) {
    std::shared_ptr<proto::RaftLogEntry> entry(new proto::RaftLogEntry);
    entry->set_add_peer(peer.ipPort());
    entry->set_sender(PeerId::self().ipPort());
    uint64_t serial_id = request_id_.getNewId();
    entry->set_sender_serial_id(serial_id);
    uint64_t append_term = raft_node_.getTerm();
    uint64_t index = raft_node_.leaderAppendLogEntry(entry);
    if (index > 0 &&
        raft_node_.waitAndCheckCommit(index, append_term, serial_id)) {
      return 1;
    }
  }
  return 0;
}

bool RaftChunk::update(const std::shared_ptr<Revision>& item) {
  CHECK(item != nullptr);
  CHECK_EQ(id(), item->getChunkId());
  writeLock();
  static_cast<RaftChunkDataRamContainer*>(data_container_.get())
      ->checkAndPrepareUpdate(LogicalTime::sample(), item);
  CHECK(raft_node_.isRunning());
  if (raftInsertRequest(item)) {
    syncLatestCommitTime(*item);
    unlock();
    return true;
  } else {
    unlock();
    return false;
  }
}

bool RaftChunk::sendConnectRequest(const PeerId& peer,
                                   const proto::ChunkRequestMetadata& metadata,
                                   proto::ConnectRequestType connect_type) {
  Message request, response;
  proto::ConnectResponse connect_response;
  connect_response.set_index(0);

  proto::RaftConnectRequest connect_request;
  connect_request.mutable_metadata()->CopyFrom(metadata);
  connect_request.set_connect_request_type(connect_type);
  request.impose<RaftNode::kConnectRequest>(connect_request);

  // TODO(aqurai): Avoid infinite loop. Use Chord index to get chunk holder
  // if request fails.
  PeerId request_peer = peer;
  while (connect_response.index() == 0) {
    if (!(Hub::instance().try_request(request_peer, &request, &response))) {
      break;
    }
    response.extract<RaftNode::kConnectResponse>(&connect_response);
    if (connect_response.index() > 0) {
      return true;
    } else if (connect_response.has_leader_id()) {
      request_peer = PeerId(connect_response.leader_id());
    }
    usleep(1000);
  }
  return false;
}

bool RaftChunk::sendChunkTransactionInfo(proto::ChunkTransactionInfo* info) {
  CHECK(raft_node_.isRunning()) << PeerId::self();
  uint64_t serial_id = request_id_.getNewId();
  // TODO(aqurai): Limit number of retry attempts.
  while (raft_node_.isRunning()) {
    if (raft_node_.sendChunkTransactionInfo(info, serial_id)) {
      return true;
    }
    usleep(150 * kMillisecondsToMicroseconds);
  }
  return false;
}

bool RaftChunk::bulkInsertLocked(const MutableRevisionMap& items,
                                 const LogicalTime& time) {
  std::vector<proto::PatchRequest> insert_requests;
  for (const MutableRevisionMap::value_type& item : items) {
    CHECK_NOTNULL(item.second.get());
    item.second->setChunkId(id());
  }
  static_cast<RaftChunkDataRamContainer*>(data_container_.get())
      ->checkAndPrepareBulkInsert(time, items);
  for (const ConstRevisionMap::value_type& item : items) {
    if (!raftInsertRequest(item.second)) {
      return false;
    }
  }
  return true;
}

bool RaftChunk::updateLocked(const LogicalTime& time,
                             const std::shared_ptr<Revision>& item) {
  CHECK(item != nullptr);
  CHECK_EQ(id(), item->getChunkId());
  static_cast<RaftChunkDataRamContainer*>(data_container_.get())
      ->checkAndPrepareUpdate(time, item);
  return raftInsertRequest(item);
}

bool RaftChunk::removeLocked(const LogicalTime& time,
                             const std::shared_ptr<Revision>& item) {
  CHECK(item != nullptr);
  CHECK_EQ(id(), item->getChunkId());
  static_cast<RaftChunkDataRamContainer*>(data_container_.get())
      ->checkAndPrepareUpdate(time, item);
  return raftInsertRequest(item);
}

LogicalTime RaftChunk::getLatestCommitTime() const {
  std::lock_guard<std::mutex> lock(latest_commit_time_mutex_);
  return latest_commit_time_;
}

bool RaftChunk::raftInsertRequest(const Revision::ConstPtr& item) {
  CHECK(raft_node_.isRunning()) << PeerId::self();
  uint64_t serial_id = request_id_.getNewId();
  // TODO(aqurai): Limit number of retry attempts.
  while (raft_node_.isRunning()) {
    if (raft_node_.sendInsertRequest(item, serial_id)) {
      break;
    }
    usleep(150 * kMillisecondsToMicroseconds);
  }
  return true;
}

void RaftChunk::insertCommitCallback(const common::Id& inserted_id) {
  handleCommitInsert(inserted_id);
}

void RaftChunk::updateCommitCallback(const common::Id& updated_id) {
  handleCommitUpdate(updated_id);
}

void RaftChunk::unlockCommitCallback() { handleCommitEnd(); }

void RaftChunk::forceStopRaft() { raft_node_.stop(); }

void RaftChunk::leaveImpl() {
  // We may stop raft node explicitly without calling leave in some tests.
  if (!raft_node_.isRunning()) {
    return;
  }
  writeLock();
  CHECK(raft_node_.isRunning());
  uint64_t serial_id = request_id_.getNewId();
  leave_requested_ = true;
  while (raft_node_.isRunning()) {
    VLOG(1) << PeerId::self() << ": Attempting to leave chunk " << id();
    bool success = raft_node_.sendLeaveRequest(serial_id);
    if (success) {
      raft_node_.stop();
      break;
    }
    usleep(150 * kMillisecondsToMicroseconds);
  }
  VLOG(1) << PeerId::self() << ": Left chunk " << id();
}

void RaftChunk::awaitShared() {}

void RaftChunk::handleRaftLeaveNotification(Message* response) {
  CHECK(leave_requested_);
  leave_notification_.notify();
  response->ack();
}

}  // namespace map_api
