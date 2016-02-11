#include <dmap/net-table.h>
#include <glog/logging.h>
#include <dmap/legacy-chunk-data-ram-container.h>
#include <dmap/legacy-chunk-data-stxxl-container.h>

#include <aslam/common/statistics/statistics.h>
#include <aslam/common/timer.h>
#include <multiagent-mapping-common/backtrace.h>

#include "dmap/core.h"
#include "dmap/hub.h"
#include "dmap/legacy-chunk.h"
#include "dmap/net-table-manager.h"
#include "dmap/transaction.h"

DEFINE_bool(use_raft, false, "Toggles use of Raft chunks.");

namespace dmap {

const std::string NetTable::kChunkIdField = "chunk_id";

const char NetTable::kPushNewChunksRequest[] = "dmap_net_table_push_new";
const char NetTable::kAnnounceToListeners[] =
    "dmap_net_table_announce_to_listeners";

MAP_API_STRING_MESSAGE(NetTable::kPushNewChunksRequest);
MAP_API_STRING_MESSAGE(NetTable::kAnnounceToListeners);

NetTable::NetTable() {}

bool NetTable::init(std::shared_ptr<TableDescriptor> descriptor) {
  descriptor_ = descriptor;
  return true;
}

void NetTable::createIndex() {
  aslam::ScopedWriteLock lock(&index_lock_);
  CHECK(index_.get() == nullptr);
  index_.reset(new NetTableIndex(name()));
  index_->create();
}

void NetTable::joinIndex(const PeerId& entry_point) {
  aslam::ScopedWriteLock lock(&index_lock_);
  CHECK(index_.get() == nullptr);
  index_.reset(new NetTableIndex(name()));
  index_->join(entry_point);
}

void NetTable::createSpatialIndex(const SpatialIndex::BoundingBox& bounds,
                                  const std::vector<size_t>& subdivision) {
  aslam::ScopedWriteLock lock(&index_lock_);
  CHECK(spatial_index_.get() == nullptr);
  spatial_index_.reset(new SpatialIndex(name(), bounds, subdivision));
  spatial_index_->create();
}

void NetTable::joinSpatialIndex(const SpatialIndex::BoundingBox& bounds,
                                const std::vector<size_t>& subdivision,
                                const PeerId& entry_point) {
  aslam::ScopedWriteLock lock(&index_lock_);
  CHECK(spatial_index_.get() == nullptr);
  spatial_index_.reset(new SpatialIndex(name(), bounds, subdivision));
  spatial_index_->join(entry_point);
}

void NetTable::announceToListeners(const PeerIdList& listeners) {
  for (const PeerId& peer : listeners) {
    Message request, response;
    request.impose<kAnnounceToListeners>(descriptor_->name());
    if (!Hub::instance().hasPeer(peer)) {
      LOG(ERROR) << "Host " << peer << " not among peers!";
      continue;
    }
    if (!Hub::instance().try_request(peer, &request, &response)) {
      // TODO(tcies) weed out unreachable peers.
      LOG(WARNING) << "Listener " << peer << " not reachable (any more?)!";
      continue;
    }
    CHECK(response.isOk());
  }
}

const std::string& NetTable::name() const { return descriptor_->name(); }

ChunkBase* NetTable::addInitializedChunk(std::unique_ptr<ChunkBase>&& chunk) {
  aslam::ScopedWriteLock lock(&active_chunks_lock_);
  std::pair<ChunkMap::iterator, bool> emplaced =
      active_chunks_.emplace(chunk->id(), std::move(chunk));
  CHECK(emplaced.second);
  ChunkBase* final_chunk_ptr = emplaced.first->second.get();
  // Attach triggers from triggers_to_attach_to_future_chunks_.
  attachTriggers(final_chunk_ptr);
  // Run callback for chunk acquisition.
  std::thread([this, final_chunk_ptr]() {
                std::lock_guard<std::mutex> lock(
                    m_chunk_acquisition_callbacks_);
                for (const ChunkAcquisitionCallback& callback :
                     chunk_acquisition_callbacks_) {
                  callback(final_chunk_ptr);
                }
              }).detach();
  return emplaced.first->second.get();
}

std::shared_ptr<Revision> NetTable::getTemplate() const {
  return descriptor_->getTemplate();
}

ChunkBase* NetTable::newChunk() {
  common::Id chunk_id;
  common::generateId(&chunk_id);
  return newChunk(chunk_id);
}

ChunkBase* NetTable::newChunk(const common::Id& chunk_id) {
  std::unique_ptr<ChunkBase> chunk;
  if (FLAGS_use_raft) {
    LOG(FATAL) << "TODO(aqurai): Implement raft chunk";
  } else {
    chunk.reset(new LegacyChunk);
  }
  chunk->initializeNew(chunk_id, descriptor_);
  ChunkBase* final_chunk_ptr = addInitializedChunk(std::move(chunk));

  joinChunkHolders(chunk_id);

  // Push chunk to listeners.
  std::lock_guard<std::mutex> l_new_chunk_listeners(m_new_chunk_listeners_);
  for (const PeerId& peer : new_chunk_listeners_) {
    if (final_chunk_ptr->requestParticipation(peer) == 0) {
      LOG(WARNING) << "Peer " << peer << ", who is listening to new chunks "
                   << " on " << name() << ", didn't receive new chunk!";
      // TODO(tcies) Find a good policy to remove stale listeners.
    }
  }
  return final_chunk_ptr;
}

ChunkBase* NetTable::getChunk(const common::Id& chunk_id) {
  aslam::timing::Timer timer("dmap::NetTable::getChunk");
  active_chunks_lock_.acquireReadLock();
  ChunkMap::iterator found = active_chunks_.find(chunk_id);
  if (found == active_chunks_.end()) {
    // look in index and connect to peers that claim to have the data
    // (for now metatable only)
    std::unordered_set<PeerId> peers;
    getChunkHolders(chunk_id, &peers);
    // Chord can possibly be inconsistent, we therefore need to remove ourself
    // from the chunk holder list if we happen to be part of it.
    LOG_IF(WARNING, peers.erase(PeerId::self()) > 0u)
        << "Peer was falsely in holders of chunk " << chunk_id;
    CHECK(!peers.empty()) << "Chunk " << chunk_id.hexString()
                          << " not available!";
    active_chunks_lock_.releaseReadLock();
    connectTo(chunk_id, *peers.begin());
    active_chunks_lock_.acquireReadLock();
    found = active_chunks_.find(chunk_id);
    CHECK(found != active_chunks_.end());
  }
  ChunkBase* result = found->second.get();
  active_chunks_lock_.releaseReadLock();
  timer.Stop();
  return result;
}

bool NetTable::ensureHasChunks(const common::IdSet& chunks_to_ensure) {
  bool success = true;
  for (const common::Id& chunk_id : chunks_to_ensure) {
    if (!getChunk(chunk_id)) {
      success = false;
      continue;
    }
  }
  return success;
}

void NetTable::pushNewChunkIdsToTracker(
    NetTable* table_of_tracking_item,
    const std::function<common::Id(const Revision&)>&
        how_to_determine_tracking_item) {
  CHECK_NOTNULL(table_of_tracking_item);
  CHECK(new_chunk_trackers_.insert(std::make_pair(
                                       table_of_tracking_item,
                                       how_to_determine_tracking_item)).second);
}

void NetTable::pushNewChunkIdsToTracker(NetTable* tracker_table) {
  CHECK_NOTNULL(tracker_table);
  auto identification_method_placeholder = [this, tracker_table](
      const Revision&) {
    LOG(FATAL) << "Override of tracker identification method (trackee = "
               << this->name() << ", tracker = " << tracker_table->name()
               << ") required!";
    return common::Id();
  };
  CHECK(new_chunk_trackers_.emplace(tracker_table,
                                    identification_method_placeholder).second);
}

template <>
void NetTable::followTrackedChunksOfItem(const common::Id& item_id,
                                         ChunkBase* tracker_chunk) {
  CHECK_NOTNULL(tracker_chunk);
  ChunkBase::TriggerCallback fetch_callback = [item_id, tracker_chunk, this](
      const common::IdSet& /*insertions*/, const common::IdSet& updates) {
    common::IdSet::const_iterator found = updates.find(item_id);
    if (found != updates.end()) {
      Transaction transaction;
      std::shared_ptr<const Revision> revision =
          transaction.getById(item_id, this, tracker_chunk);
      revision->fetchTrackedChunks();
    }
  };
  tracker_chunk->attachTrigger(fetch_callback);
  // Fetch tracked chunks now.
  fetch_callback(common::IdSet(), common::IdSet({item_id}));
}

void NetTable::autoFollowTrackedChunks() {
  // I suggest to leave this message here as a warning to future generations
  // that might have similarly terrible ideas.
  LOG(FATAL)
      << "autoFollowTrackedChunks is flawed since it can cause the "
      << "presence of an inconsistent set of chunks in concurrent views. "
      << "Revision::fetchTrackedChunks() should instead be called at the "
      << "beginning of transactions.";
}

const std::vector<Revision::AutoMergePolicy>& NetTable::getAutoMergePolicies()
    const {
  return auto_merge_policies_;
}

void NetTable::addAutoMergePolicy(
    const Revision::AutoMergePolicy& auto_merge_policy) {
  CHECK(auto_merge_policy);
  auto_merge_policies_.push_back(auto_merge_policy);
}

void NetTable::registerChunkInSpace(
    const common::Id& chunk_id, const SpatialIndex::BoundingBox& bounding_box) {
  active_chunks_lock_.acquireReadLock();
  CHECK(active_chunks_.find(chunk_id) != active_chunks_.end());
  active_chunks_lock_.releaseReadLock();
  aslam::ScopedReadLock lock(&index_lock_);
  spatial_index_->announceChunk(chunk_id, bounding_box);
}

void NetTable::getChunkReferencesInBoundingBox(
    const SpatialIndex::BoundingBox& bounding_box,
    std::unordered_set<common::Id>* chunk_ids) {
  CHECK_NOTNULL(chunk_ids);
  aslam::timing::Timer seek_timer(
      "dmap::NetTable::getChunksInBoundingBox - seek");
  {
    aslam::ScopedReadLock lock(&index_lock_);
    spatial_index_->seekChunks(bounding_box, chunk_ids);
  }
  seek_timer.Stop();
  aslam::statistics::StatsCollector collector(
      "dmap::NetTable::getChunksInBoundingBox - chunks");
  collector.AddSample(chunk_ids->size());
}

void NetTable::getChunksInBoundingBox(
    const SpatialIndex::BoundingBox& bounding_box) {
  std::unordered_set<ChunkBase*> dummy;
  getChunksInBoundingBox(bounding_box, &dummy);
}

void NetTable::getChunksInBoundingBox(
    const SpatialIndex::BoundingBox& bounding_box,
    std::unordered_set<ChunkBase*>* chunks) {
  CHECK_NOTNULL(chunks);
  chunks->clear();
  std::unordered_set<common::Id> chunk_ids;
  getChunkReferencesInBoundingBox(bounding_box, &chunk_ids);
  for (const common::Id& id : chunk_ids) {
    ChunkBase* chunk = getChunk(id);
    CHECK_NOTNULL(chunk);
    chunks->insert(chunk);
  }
  VLOG(5) << "Got " << chunk_ids.size() << " chunks";
}

void NetTable::attachTriggerToCurrentAndFutureChunks(
    const TriggerCallbackWithChunkPointer& callback) {
  CHECK(callback);
  // Make sure no chunks are added during the execution of this.
  aslam::ScopedReadLock lock(&active_chunks_lock_);
  // Make sure future chunks will get the trigger attached.
  {
    std::lock_guard<std::mutex> attach_lock(m_triggers_to_attach_);
    triggers_to_attach_to_future_chunks_.push_back(callback);
  }
  // Attach trigger to all current chunks.
  for (const ChunkMap::value_type& id_chunk : active_chunks_) {
    ChunkBase* chunk = id_chunk.second.get();
    chunk->attachTrigger([chunk, callback](const common::IdSet& insertions,
                                           const common::IdSet& updates) {
      callback(insertions, updates, chunk);
    });
  }
}

void NetTable::attachCallbackToChunkAcquisition(
    const ChunkAcquisitionCallback& callback) {
  CHECK(callback);
  // Make sure no chunks are added during the execution of this.
  aslam::ScopedReadLock lock(&active_chunks_lock_);
  std::lock_guard<std::mutex> attach_lock(m_chunk_acquisition_callbacks_);
  chunk_acquisition_callbacks_.push_back(callback);
}

bool NetTable::listenToChunksFromPeer(const PeerId& peer) {
  Message request, response;
  request.impose<NetTable::kPushNewChunksRequest>(descriptor_->name());
  if (!Hub::instance().hasPeer(peer)) {
    LOG(ERROR) << "Peer with address " << peer << " not among peers!";
    return false;
  }
  Hub::instance().request(peer, &request, &response);
  if (!response.isOk()) {
    LOG(ERROR) << "Peer " << peer << " refused to share chunks!";
    return false;
  }
  return true;
}

void NetTable::handleListenToChunksFromPeer(const PeerId& listener,
                                            Message* response) {
  aslam::ScopedReadLock chunk_lock(&active_chunks_lock_);
  std::set<ChunkBase*> chunks_to_share_now;
  // Assumes read lock can be recursive (which it currently can).
  getActiveChunks(&chunks_to_share_now);

  std::lock_guard<std::mutex> l_new_chunk_listeners(m_new_chunk_listeners_);
  new_chunk_listeners_.emplace(listener);

  // Never call and RPC in an RPC handler.
  // Variables must be passed by copy, as they go out of scope.
  // Danger: Assumes chunks are not released in the meantime.
  // TODO(tcies) add a lock for removing chunks?
  std::thread previous_sharer([this, listener, chunks_to_share_now]() {
    for (ChunkBase* chunk : chunks_to_share_now) {
      CHECK_EQ(chunk->requestParticipation(listener), 1);
    }
  });
  previous_sharer.detach();

  response->ack();
}

bool NetTable::insert(const LogicalTime& time, ChunkBase* chunk,
                      const std::shared_ptr<Revision>& query) {
  CHECK_NOTNULL(chunk);
  CHECK(query != nullptr);
  CHECK(chunk->insert(time, query));
  return true;
}

bool NetTable::update(const std::shared_ptr<Revision>& query) {
  CHECK(query != nullptr);
  CHECK_NOTNULL(getChunk(query->getChunkId()))->update(query);
  return true;
}

void NetTable::dumpActiveChunks(const LogicalTime& time,
                                ConstRevisionMap* destination) {
  CHECK_NOTNULL(destination);
  destination->clear();
  std::set<common::Id> active_chunk_ids;
  getActiveChunkIds(&active_chunk_ids);
  for (const common::Id& chunk_id : active_chunk_ids) {
    ConstRevisionMap chunk_revisions;
    dmap::ChunkBase* chunk = getChunk(chunk_id);
    CHECK_NOTNULL(chunk);
    chunk->dumpItems(time, &chunk_revisions);
    destination->insert(chunk_revisions.begin(), chunk_revisions.end());
  }
}

void NetTable::dumpActiveChunksAtCurrentTime(ConstRevisionMap* destination) {
  CHECK_NOTNULL(destination);
  return dumpActiveChunks(dmap::LogicalTime::sample(), destination);
}

ChunkBase* NetTable::connectTo(const common::Id& chunk_id, const PeerId& peer) {
  Message request, response;
  // sends request of chunk info to peer
  proto::ChunkRequestMetadata metadata;
  metadata.set_table(descriptor_->name());
  chunk_id.serialize(metadata.mutable_chunk_id());
  request.impose<LegacyChunk::kConnectRequest>(metadata);
  // TODO(tcies) add to local peer subset as well?
  VLOG(5) << "Connecting to " << peer << " for chunk " << chunk_id;
  Hub::instance().request(peer, &request, &response);
  CHECK(response.isType<Message::kAck>()) << response.type();
  // wait for connect handle thread of other peer to succeed
  ChunkMap::iterator found;
  while (true) {
    active_chunks_lock_.acquireReadLock();
    found = active_chunks_.find(chunk_id);
    if (found != active_chunks_.end()) {
      active_chunks_lock_.releaseReadLock();
      break;
    }
    active_chunks_lock_.releaseReadLock();
    usleep(1000);
  }
  return found->second.get();
}

size_t NetTable::numActiveChunks() const {
  active_chunks_lock_.acquireReadLock();
  size_t result = active_chunks_.size();
  active_chunks_lock_.releaseReadLock();
  return result;
}

size_t NetTable::numActiveChunksItems() {
  std::set<common::Id> active_chunk_ids;
  getActiveChunkIds(&active_chunk_ids);
  size_t num_elements = 0;
  LogicalTime now = LogicalTime::sample();
  for (const common::Id& chunk_id : active_chunk_ids) {
    ChunkBase* chunk = getChunk(chunk_id);
    CHECK_NOTNULL(chunk);
    num_elements += chunk->numItems(now);
  }
  return num_elements;
}

size_t NetTable::numItems() const {
  size_t result = 0;
  LogicalTime count_time = LogicalTime::sample();
  forEachActiveChunk([&](const ChunkBase& chunk) {
    result += chunk.constData()->numAvailableIds(count_time);
  });
  return result;
}

size_t NetTable::activeChunksItemsSizeBytes() {
  std::set<common::Id> active_chunk_ids;
  getActiveChunkIds(&active_chunk_ids);
  size_t size_bytes = 0;
  LogicalTime now = LogicalTime::sample();
  for (const common::Id& chunk_id : active_chunk_ids) {
    ChunkBase* chunk = getChunk(chunk_id);
    CHECK_NOTNULL(chunk);
    size_bytes += chunk->itemsSizeBytes(now);
  }
  return size_bytes;
}

void NetTable::kill() {
  leaveAllChunks();
  leaveIndices();
}

void NetTable::killOnceShared() {
  leaveAllChunksOnceShared();
  leaveIndices();
}

void NetTable::shareAllChunks() {
  active_chunks_lock_.acquireReadLock();
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    chunk.second->requestParticipation();
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::shareAllChunks(const PeerId& peer) {
  active_chunks_lock_.acquireReadLock();
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    chunk.second->requestParticipation(peer);
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::leaveAllChunks() {
  active_chunks_lock_.acquireReadLock();
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    chunk.second->leave();
    leaveChunkHolders(chunk.first);
  }
  CHECK(active_chunks_lock_.upgradeToWriteLock());
  active_chunks_.clear();
  active_chunks_lock_.releaseWriteLock();
}

void NetTable::leaveAllChunksOnceShared() {
  active_chunks_lock_.acquireReadLock();
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    chunk.second->leaveOnceShared();
    leaveChunkHolders(chunk.first);
  }
  CHECK(active_chunks_lock_.upgradeToWriteLock());
  active_chunks_.clear();
  active_chunks_lock_.releaseWriteLock();
}

std::string NetTable::getStatistics() {
  std::stringstream ss;
  ss << name() << ": " << numActiveChunks() << " chunks and "
     << numActiveChunksItems() << " items. ["
     << humanReadableBytes(activeChunksItemsSizeBytes()) << "]";
  return ss.str();
}

void NetTable::getActiveChunkIds(std::set<common::Id>* chunk_ids) const {
  CHECK_NOTNULL(chunk_ids);
  chunk_ids->clear();
  active_chunks_lock_.acquireReadLock();
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    chunk_ids->insert(chunk.first);
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::getActiveChunks(std::set<ChunkBase*>* chunks) const {
  CHECK_NOTNULL(chunks);
  chunks->clear();
  active_chunks_lock_.acquireReadLock();
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    chunks->insert(chunk.second.get());
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::readLockActiveChunks() {
  active_chunks_lock_.acquireReadLock();
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    chunk.second->readLock();
  }
}

void NetTable::unlockActiveChunks() {
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    chunk.second->unlock();
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::forEachActiveChunk(
    const std::function<void(const ChunkBase& chunk)>& action) const {
  aslam::ScopedReadLock lock(&active_chunks_lock_);
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    action(*chunk.second);
  }
}

void NetTable::forEachActiveChunkUntil(const std::function<
    bool(const ChunkBase& chunk)>& action) const {  // NOLINT
  aslam::ScopedReadLock lock(&active_chunks_lock_);
  for (const ChunkMap::value_type& chunk : active_chunks_) {
    if (action(*chunk.second)) {
      break;
    }
  }
}

void NetTable::handleConnectRequest(const common::Id& chunk_id,
                                    const PeerId& peer,
                                    Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.acquireReadLock();
  if (routingBasics(chunk_id, response, &found)) {
    LegacyChunk* chunk = CHECK_NOTNULL(
        dynamic_cast<LegacyChunk*>(found->second.get()));  // NOLINT
    chunk->handleConnectRequest(peer, response);
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::handleInitRequest(
    const proto::InitRequest& request, const PeerId& sender,
    Message* response) {
  CHECK_NOTNULL(response);
  common::Id chunk_id(request.metadata().chunk_id());
  std::unique_ptr<LegacyChunk> chunk =
      std::unique_ptr<LegacyChunk>(new LegacyChunk);
  CHECK(chunk->init(chunk_id, request, sender, descriptor_));
  addInitializedChunk(std::move(chunk));
  response->ack();
  std::thread(&NetTable::joinChunkHolders, this, chunk_id).detach();
}

void NetTable::handleInsertRequest(const common::Id& chunk_id,
                                   const std::shared_ptr<Revision>& item,
                                   Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.acquireReadLock();
  if (routingBasics(chunk_id, response, &found)) {
    LegacyChunk* chunk = CHECK_NOTNULL(
        dynamic_cast<LegacyChunk*>(found->second.get()));  // NOLINT
    chunk->handleInsertRequest(item, response);
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::handleLeaveRequest(
    const common::Id& chunk_id, const PeerId& leaver, Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.acquireReadLock();
  if (routingBasics(chunk_id, response, &found)) {
    LegacyChunk* chunk = CHECK_NOTNULL(
        dynamic_cast<LegacyChunk*>(found->second.get()));  // NOLINT
    chunk->handleLeaveRequest(leaver, response);
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::handleLockRequest(
    const common::Id& chunk_id, const PeerId& locker, Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.acquireReadLock();
  if (routingBasics(chunk_id, response, &found)) {
    LegacyChunk* chunk = CHECK_NOTNULL(
        dynamic_cast<LegacyChunk*>(found->second.get()));  // NOLINT
    chunk->handleLockRequest(locker, response);
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::handleNewPeerRequest(
    const common::Id& chunk_id, const PeerId& peer, const PeerId& sender,
    Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.acquireReadLock();
  if (routingBasics(chunk_id, response, &found)) {
    LegacyChunk* chunk = CHECK_NOTNULL(
        dynamic_cast<LegacyChunk*>(found->second.get()));  // NOLINT
    chunk->handleNewPeerRequest(peer, sender, response);
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::handleUnlockRequest(
    const common::Id& chunk_id, const PeerId& locker, Message* response) {
  ChunkMap::iterator found;
  active_chunks_lock_.acquireReadLock();
  if (routingBasics(chunk_id, response, &found)) {
    LegacyChunk* chunk = CHECK_NOTNULL(
        dynamic_cast<LegacyChunk*>(found->second.get()));  // NOLINT
    chunk->handleUnlockRequest(locker, response);
  }
  active_chunks_lock_.releaseReadLock();
}

void NetTable::handleUpdateRequest(const common::Id& chunk_id,
                                   const std::shared_ptr<Revision>& item,
                                   const PeerId& sender, Message* response) {
  ChunkMap::iterator found;
  if (routingBasics(chunk_id, response, &found)) {
    LegacyChunk* chunk = CHECK_NOTNULL(
        dynamic_cast<LegacyChunk*>(found->second.get()));  // NOLINT
    chunk->handleUpdateRequest(item, sender, response);
  }
}

void NetTable::handleRoutedNetTableChordRequests(const Message& request,
                                                 Message* response) {
  aslam::ScopedReadLock lock(&index_lock_);
  if (index_) {
    index_->handleRoutedRequest(request, response);
  } else {
    response->decline();
  }
}

void NetTable::handleRoutedSpatialChordRequests(const Message& request,
                                                Message* response) {
  aslam::ScopedReadLock lock(&index_lock_);
  if (spatial_index_) {
    spatial_index_->handleRoutedRequest(request, response);
  } else {
    response->decline();
  }
}

void NetTable::handleAnnounceToListeners(const PeerId& announcer,
                                         Message* response) {
  // Never call an RPC in an RPC handler.
  std::thread(&NetTable::listenToChunksFromPeer, this, announcer).detach();
  response->ack();
}

void NetTable::handleSpatialIndexTrigger(
    const proto::SpatialIndexTrigger& trigger) {
  VLOG(5) << "Received spatial index trigger with " << trigger.new_chunks_size()
          << " new chunks";
  for (int i = 0; i < trigger.new_chunks_size(); ++i) {
    common::Id chunk_id(trigger.new_chunks(i));
    std::thread([this, chunk_id]() { CHECK_NOTNULL(getChunk(chunk_id)); })
        .detach();
  }
}

bool NetTable::routingBasics(
    const common::Id& chunk_id, Message* response, ChunkMap::iterator* found) {
  CHECK_NOTNULL(response);
  CHECK_NOTNULL(found);
  *found = active_chunks_.find(chunk_id);
  if (*found == active_chunks_.end()) {
    LOG(WARNING) << "In " << name() << ", couldn't find " << chunk_id
                 << " among:";
    for (const ChunkMap::value_type& chunk : active_chunks_) {
      LOG(WARNING) << chunk.second->id();
    }
    response->impose<Message::kDecline>();
    return false;
  }
  return true;
}

void NetTable::attachTriggers(ChunkBase* chunk) {
  CHECK_NOTNULL(chunk);
  std::lock_guard<std::mutex> lock(m_triggers_to_attach_);
  if (!triggers_to_attach_to_future_chunks_.empty()) {
    for (const TriggerCallbackWithChunkPointer& trigger :
         triggers_to_attach_to_future_chunks_) {
      chunk->attachTrigger([trigger, chunk](const common::IdSet& insertions,
                                            const common::IdSet& updates) {
        trigger(insertions, updates, chunk);
      });
    }
  }
}

void NetTable::leaveIndices() {
  index_lock_.acquireReadLock();
  if (index_.get() != nullptr) {
    VLOG(1) << PeerId::self() << " leaving index for table " << name();
    index_->leave();
    CHECK(index_lock_.upgradeToWriteLock());
    index_.reset();
    index_lock_.releaseWriteLock();
  } else {
    index_lock_.releaseReadLock();
  }
  index_lock_.acquireReadLock();
  if (spatial_index_.get() != nullptr) {
    spatial_index_->leave();
    CHECK(index_lock_.upgradeToWriteLock());
    spatial_index_.reset();
    index_lock_.releaseWriteLock();
  } else {
    index_lock_.releaseReadLock();
  }
}

void NetTable::getChunkHolders(const common::Id& chunk_id,
                               std::unordered_set<PeerId>* peers) {
  CHECK_NOTNULL(peers);
  aslam::ScopedReadLock lock(&index_lock_);
  CHECK_NOTNULL(index_.get());
  index_->seekPeers(chunk_id, peers);
}

void NetTable::joinChunkHolders(const common::Id& chunk_id) {
  aslam::ScopedReadLock lock(&index_lock_);
  CHECK_NOTNULL(index_.get());
  VLOG(5) << "Joining " << chunk_id.hexString() << " holders";
  index_->announcePosession(chunk_id);
}

void NetTable::leaveChunkHolders(const common::Id& chunk_id) {
  aslam::ScopedReadLock lock(&index_lock_);
  CHECK_NOTNULL(index_.get());
  VLOG(5) << "Leaving " << chunk_id.hexString() << " holders";
  index_->renouncePosession(chunk_id);
}

}  // namespace dmap
