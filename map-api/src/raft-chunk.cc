#include <map-api/raft-chunk.h>

#include "./core.pb.h"
#include "./chunk.pb.h"
#include "map-api/chunk-data-ram-container.h"
#include "map-api/raft-node.h"
#include "map-api/hub.h"
#include "map-api/message.h"
#include "map-api/net-table-manager.h"

namespace map_api {

RaftChunk::~RaftChunk() {}

bool RaftChunk::init(const common::Id& id, std::shared_ptr<TableDescriptor> descriptor,
            bool initialize) {
  id_ = id;
  // TODO(aqurai): init data container.
  data_container_.reset(new ChunkDataRamContainer);
  CHECK(data_container_->init(descriptor));
  initialized_ = true;
  raft_node_.chunk_id_ = id_;
  raft_node_.table_name_ = descriptor->name();
  return true;
}

void RaftChunk::initializeNewImpl(
    const common::Id& id, const std::shared_ptr<TableDescriptor>& descriptor) {
  CHECK(init(id, descriptor, true));

  VLOG(1) << " INIT chunk at peer " << PeerId::self() << " in table "
          << raft_node_.table_name_;
  setStateFollowerAndStart();
}

bool RaftChunk::init(const common::Id& id, const PeerId& peer,
                     std::shared_ptr<TableDescriptor> descriptor) {
  CHECK(init(id, descriptor, true));
  setStateJoiningAndStart(peer);
}

bool RaftChunk::init(const common::Id& id,
                     std::shared_ptr<TableDescriptor> descriptor) {
  CHECK(init(id, descriptor, true));
  setStateFollowerAndStart();
  return true;
}

void RaftChunk::dumpItems(const LogicalTime& time, ConstRevisionMap* items) const {
  CHECK_NOTNULL(items);
  data_container_->dump(time, items);
}

void RaftChunk::handleConnectRequest(const Message& request,
                                     Message* response) {}

void RaftChunk::handleRaftAppendRequest(const common::Id& chunk_id,
                                        const Message& request,
                                        Message* response) {
  CHECK(chunk_id == id_);
  raft_node_.handleAppendRequest(request, response);
}

void RaftChunk::handleRaftRequestVote(const common::Id& chunk_id,
                                      const Message& request,
                                      Message* response) {
  CHECK(chunk_id == id_);
  raft_node_.handleRequestVote(request, response);
}

void RaftChunk::handleRaftQueryState(const common::Id& chunk_id,
                                     const Message& request,
                                     Message* response) {
  CHECK(chunk_id == id_);
  raft_node_.handleQueryState(request, response);
}

void RaftChunk::handleRaftJoinQuitRequest(const common::Id& chunk_id,
                                          const Message& request,
                                          Message* response) {
  CHECK(chunk_id == id_);
  raft_node_.handleJoinQuitRequest(request, response);
}

void RaftChunk::handleRaftNotifyJoinQuitSuccess(const common::Id& chunk_id,
                                                const Message& request,
                                                Message* response) {
  CHECK(chunk_id == id_);
  raft_node_.handleNotifyJoinQuitSuccess(request, response);
}




}  // namespace map_api
