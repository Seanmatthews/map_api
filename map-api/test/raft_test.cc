#include <set>
#include <sys/types.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <multiagent-mapping-common/conversions.h>

#include "map-api/hub.h"
#include "map-api/ipc.h"
#include "map-api/peer-id.h"
#include "map-api/raft-node.h"
#include "map-api/raft-chunk.h"
#include "map-api/test/testing-entrypoint.h"
#include "./consensus_fixture.h"
#include "./net_table_fixture.h"

namespace map_api {

constexpr int kWaitTimeMs = 1000;
constexpr int kAppendEntriesForMs = 10000;
constexpr int kTimeBetweenAppendsMs = 20;
constexpr int kNumEntriesToAppend = 40;

DEFINE_uint64(raft_chunk_processes, 5u,
              "Total number of processes in RaftChunkTests");
/*
TEST_F(ConsensusFixture, RaftGetChunk) {
  const uint64_t kProcesses = FLAGS_raft_chunk_processes;
  enum Barriers {
    INIT_PEERS,
    PUSH_CHUNK_ID,
    CHUNKS_INIT,
    DIE
  };
  pid_t pid = getpid();
  VLOG(1) << "PID: " << pid << ", IP: " << PeerId::self();
  if (getSubprocessId() == 0) {
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i);
    }
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    LOG(WARNING) << "Creating a new chunk.";
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    ChunkBase* base_chunk = table_->newChunk();
    LOG(WARNING) << "Created a new chunk " << base_chunk->id();
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::push(chunk->id());
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);

    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    LOG(WARNING) << "Chunks initialized on all peers";
    EXPECT_EQ(kProcesses - 1, chunk->peerSize());
    IPC::barrier(DIE, kProcesses - 1);
  } else {
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);
    common::Id chunk_id = IPC::pop<common::Id>();
    ChunkBase* base_chunk = table_->getChunk(chunk_id);
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    IPC::barrier(DIE, kProcesses - 1);
  }
}

TEST_F(ConsensusFixture, RaftRequestParticipation) {
  const uint64_t kProcesses = FLAGS_raft_chunk_processes;
  enum Barriers {
    INIT_PEERS,
    PUSH_CHUNK_ID,
    REQUESTED_PARTICIPATION,
    CHUNKS_INIT,
    DIE
  };
  pid_t pid = getpid();
  VLOG(1) << "PID: " << pid << ", IP: " << PeerId::self();
  if (getSubprocessId() == 0) {
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i);
    }
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    LOG(WARNING) << "Creating a new chunk.";
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    ChunkBase* base_chunk = table_->newChunk();
    LOG(WARNING) << "Created a new chunk " << base_chunk->id();
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::push(chunk->id());
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);
    chunk->requestParticipation();
    IPC::barrier(REQUESTED_PARTICIPATION, kProcesses - 1);

    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    LOG(WARNING) << "Chunks initialized on all peers";
    EXPECT_EQ(kProcesses - 1, chunk->peerSize());
    IPC::barrier(DIE, kProcesses - 1);
  } else {
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);
    common::Id chunk_id = IPC::pop<common::Id>();

    IPC::barrier(REQUESTED_PARTICIPATION, kProcesses - 1);
    std::set<ChunkBase*> chunks;
    table_->getActiveChunks(&chunks);
    EXPECT_EQ(1, chunks.size());
    ChunkBase* base_chunk = *(chunks.begin());
    EXPECT_TRUE(chunk_id == base_chunk->id());
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);

    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    EXPECT_EQ(kProcesses - 1, chunk->peerSize());
    IPC::barrier(DIE, kProcesses - 1);
  }
}

TEST_F(ConsensusFixture, LeaderElection) {
  const uint64_t kProcesses = FLAGS_raft_chunk_processes;
  enum Barriers {
    INIT_PEERS,
    PUSH_CHUNK_ID,
    CHUNKS_INIT,
    LEADER_IP_SENT,
    DIE
  };
  pid_t pid = getpid();
  VLOG(1) << "PID: " << pid << ", IP: " << PeerId::self();
  if (getSubprocessId() == 0) {
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i);
    }
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    LOG(WARNING) << "Creating a new chunk.";
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    ChunkBase* base_chunk = table_->newChunk();
    LOG(WARNING) << "Created a new chunk " << base_chunk->id();
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::push(chunk->id());
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);
    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    LOG(WARNING) << "Chunks initialized on all peers";
    EXPECT_EQ(kProcesses - 1, chunk->peerSize());
    chunk->raft_node_.giveUpLeadership();

    // Wait for a new leader
    while (!chunk->raft_node_.getLeader().isValid()) {
      LOG(WARNING) << "Waiting for a new leader ... ";
      usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    }
    IPC::push(chunk->raft_node_.getLeader());
    IPC::barrier(LEADER_IP_SENT, kProcesses - 1);

    IPC::barrier(DIE, kProcesses - 1);
  } else {
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);
    common::Id chunk_id = IPC::pop<common::Id>();
    ChunkBase* base_chunk = table_->getChunk(chunk_id);
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    IPC::barrier(LEADER_IP_SENT, kProcesses - 1);
    PeerId leader = IPC::pop<PeerId>();
    EXPECT_EQ(leader.ipPort(), chunk->raft_node_.getLeader().ipPort());
    IPC::barrier(DIE, kProcesses - 1);
  }
}

TEST_F(ConsensusFixture, UnannouncedLeave) {
  const uint64_t kProcesses = FLAGS_raft_chunk_processes;
  enum Barriers {
    INIT_PEERS,
    PUSH_CHUNK_ID,
    CHUNKS_INIT,
    ONE_LEFT,
    DIE
  };
  enum Peers {
    LEADER,
    LEAVING_PEER
  };
  pid_t pid = getpid();
  VLOG(1) << "PID: " << pid << ", IP: " << PeerId::self();
  if (getSubprocessId() == LEADER) {
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i);
    }
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    LOG(WARNING) << "Creating a new chunk.";
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    ChunkBase* base_chunk = table_->newChunk();
    LOG(WARNING) << "Created a new chunk " << base_chunk->id();
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::push(chunk->id());
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);

    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    LOG(WARNING) << "Chunks initialized on all peers";
    EXPECT_EQ(kProcesses - 1, chunk->peerSize());
    IPC::barrier(ONE_LEFT, kProcesses - 1);
    EXPECT_EQ(kProcesses - 2, chunk->peerSize());
    IPC::barrier(DIE, kProcesses - 1);
  } else {
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);
    common::Id chunk_id = IPC::pop<common::Id>();
    ChunkBase* base_chunk = table_->getChunk(chunk_id);
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    EXPECT_EQ(kProcesses - 1, chunk->peerSize());

    // One Peer leaves unannounced.
    if (getSubprocessId() == LEAVING_PEER) {
      quitRaftUnannounced(chunk);
    }
    usleep(5 * kWaitTimeMs * kMillisecondsToMicroseconds);
    IPC::barrier(ONE_LEFT, kProcesses - 1);
    EXPECT_EQ(kProcesses - 2, chunk->peerSize());
    IPC::barrier(DIE, kProcesses - 1);
  }
}
*/
DEFINE_uint64(num_appends, 50u, "Total number entries to append");

TEST_F(ConsensusFixture, AppendLogEntries) {
  const uint64_t kProcesses = FLAGS_raft_chunk_processes;
  enum Barriers {
    INIT_PEERS,
    PUSH_CHUNK_ID,
    CHUNKS_INIT,
    START_APPEND,
    END_APPEND,
    STOP_RAFT,
    DIE
  };
  pid_t pid = getpid();
  VLOG(1) << "PID: " << pid << ", IP: " << PeerId::self();
  if (getSubprocessId() == 0) {
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i);
    }
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    LOG(WARNING) << "Creating a new chunk.";
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    ChunkBase* base_chunk = table_->newChunk();
    LOG(WARNING) << "Created a new chunk " << base_chunk->id();
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::push(chunk->id());
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);

    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    LOG(WARNING) << "Chunks initialized on all peers";
    EXPECT_EQ(kProcesses - 1, chunk->peerSize());
    IPC::barrier(START_APPEND, kProcesses - 1);
    for (uint i = 0; i < FLAGS_num_appends; ++i) {
      leaderAppendBlankLogEntry(chunk);
    }
    leaderWaitUntilAllCommitted(chunk);
    IPC::push(PeerId::self());
    IPC::barrier(END_APPEND, kProcesses - 1);
    EXPECT_EQ(FLAGS_num_appends, getLatestEntrySerialId(chunk, PeerId::self()));

    IPC::barrier(STOP_RAFT, kProcesses - 1);
    NetTableManager::instance().forceStopAllRaftChunks();

    IPC::barrier(DIE, kProcesses - 1);
  } else {
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);
    common::Id chunk_id = IPC::pop<common::Id>();
    ChunkBase* base_chunk = table_->getChunk(chunk_id);
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    IPC::barrier(START_APPEND, kProcesses - 1);
    IPC::barrier(END_APPEND, kProcesses - 1);
    PeerId leader_id = IPC::pop<PeerId>();
    usleep(2*kWaitTimeMs*kMillisecondsToMicroseconds);
    EXPECT_EQ(FLAGS_num_appends, getLatestEntrySerialId(chunk, leader_id));

    IPC::barrier(STOP_RAFT, kProcesses - 1);
    NetTableManager::instance().forceStopAllRaftChunks();

    IPC::barrier(DIE, kProcesses - 1);
  }
}

TEST_F(ConsensusFixture, AppendLogEntriesWithPeerLeave) {
  const uint64_t kProcesses = FLAGS_raft_chunk_processes;
  enum Barriers {
    INIT_PEERS,
    PUSH_CHUNK_ID,
    CHUNKS_INIT,
    START_APPEND,
    END_APPEND,
    DIE
  };
  enum Peers {
    LEADER,
    LEAVING_PEER,
  };
  pid_t pid = getpid();
  VLOG(1) << "PID: " << pid << ", IP: " << PeerId::self();
  if (getSubprocessId() == LEADER) {
    for (uint64_t i = 1u; i < kProcesses; ++i) {
      launchSubprocess(i);
    }
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    LOG(WARNING) << "Creating a new chunk.";
    usleep(kWaitTimeMs * kMillisecondsToMicroseconds);
    ChunkBase* base_chunk = table_->newChunk();
    LOG(WARNING) << "Created a new chunk " << base_chunk->id();
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::push(chunk->id());
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);

    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    LOG(WARNING) << "Chunks initialized on all peers";
    EXPECT_EQ(kProcesses - 1, chunk->peerSize());
    IPC::barrier(START_APPEND, kProcesses - 1);

    // Append entries and wait until the are committed.
    for (uint i = 0; i < FLAGS_num_appends; ++i) {
      leaderAppendBlankLogEntry(chunk);
    }
    leaderWaitUntilAllCommitted(chunk);
    IPC::push(PeerId::self());
    IPC::barrier(END_APPEND, kProcesses - 1);
    EXPECT_EQ(FLAGS_num_appends, getLatestEntrySerialId(chunk, PeerId::self()));
    IPC::barrier(DIE, kProcesses - 1);
  } else {
    IPC::barrier(INIT_PEERS, kProcesses - 1);
    IPC::barrier(PUSH_CHUNK_ID, kProcesses - 1);
    common::Id chunk_id = IPC::pop<common::Id>();
    ChunkBase* base_chunk = table_->getChunk(chunk_id);
    RaftChunk* chunk = dynamic_cast<RaftChunk*>(base_chunk);
    CHECK_NOTNULL(chunk);
    IPC::barrier(CHUNKS_INIT, kProcesses - 1);
    IPC::barrier(START_APPEND, kProcesses - 1);

    // One peer leaves unannounced.
    if (getSubprocessId() == LEAVING_PEER) {
      quitRaftUnannounced(chunk);
    }
    IPC::barrier(END_APPEND, kProcesses - 1);
    PeerId leader_id = IPC::pop<PeerId>();
    if (getSubprocessId() != LEAVING_PEER) {
      usleep(2*kWaitTimeMs*kMillisecondsToMicroseconds);
      EXPECT_EQ(FLAGS_num_appends, getLatestEntrySerialId(chunk, leader_id));
    }
    IPC::barrier(DIE, kProcesses - 1);
  }
}

}  // namespace map_api

MAP_API_UNITTEST_ENTRYPOINT
