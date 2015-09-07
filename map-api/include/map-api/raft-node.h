/* Notes:
 * Things raft should be doing
 *    - Send heartbeats to all peers if leader
 *    - Handle heartbeat timeouts if follower, hold election
 *    - Heartbeats from leaders include term number, log entry info
 *    - Handle RPC from clients
 *    - Send new log entries/chunk revisions to all peers
 *
 * CURRENT ASSUMPTIONS:
 * - A peer can reach all other peers, or none.
 *    i.e, no network partitions, and no case where a peer can contact some
 *    peers and not others.
 * - No malicious peers!
 *
 * -------------------------
 * Lock acquisition ordering
 * -------------------------
 * 1. state_mutex_
 * 2. log_mutex_
 * 3. peer_mutex_
 * 4. follower_tracker_mutex_
 * 5. last_heartbeat_mutex_
 * 6. last_log_index_mutex_- to be used only in leaderAppendLogEntryLocked
 *                            and followerTrackerThread, and NO WHERE else.
 *
 * --------------------------------------------------------------
 *  TODO List at this point
 * --------------------------------------------------------------
 *
 * PENDING: Handle peers who don't respond to vote rpc
 * PENDING: Values for timeout
 */

#ifndef MAP_API_RAFT_NODE_H_
#define MAP_API_RAFT_NODE_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest_prod.h>
#include <multiagent-mapping-common/reader-writer-lock.h>
#include <multiagent-mapping-common/unique-id.h>

#include "./raft.pb.h"
#include "map-api/multi-chunk-transaction.h"
#include "map-api/peer-id.h"
#include "map-api/revision.h"

#include "map-api/raft-chunk-data-ram-container.h"

namespace map_api {
class Message;
class RaftChunk;
class MultiChunkTransaction;

namespace benchmarks {
// Forward declaration necessary for friending this class.
class RaftBenchmarkTests;
class BubbleSortTest;
}

// Implementation of Raft consensus algorithm presented here:
// https://raftconsensus.github.io, http://ramcloud.stanford.edu/raft.pdf
class RaftNode {
 public:
  enum class State {
    INITIALIZING,
    JOINING,
    FOLLOWER,
    CANDIDATE,
    LEADER,
    LOST_CONNECTION,
    DISCONNECTING
  };

  void start();
  void stop();
  inline bool isRunning() const { return state_thread_running_; }
  uint64_t getTerm() const;
  const PeerId& getLeader() const;
  State getState() const;

  // Returns index of the appended entry if append succeeds, or zero otherwise
  uint64_t leaderAppendLogEntry(
      const std::shared_ptr<proto::RaftLogEntry>& new_entry);

  static const char kAppendEntries[];
  static const char kAppendEntriesResponse[];
  static const char kChunkLockRequest[];
  static const char kChunkLockResponse[];
  static const char kChunkUnlockRequest[];
  static const char kChunkUnlockResponse[];
  static const char kChunkTransactionInfo[];
  static const char kInsertRequest[];
  static const char kInsertResponse[];
  static const char kVoteRequest[];
  static const char kVoteResponse[];
  static const char kLeaveRequest[];
  static const char kLeaveNotification[];
  static const char kRaftChunkRequestResponse[];
  static const char kQueryState[];
  static const char kQueryStateResponse[];
  static const char kConnectRequest[];
  static const char kConnectResponse[];
  static const char kInitRequest[];

  static const std::string kRaftLogEntryAddPeer;
  static const std::string kRaftLogEntryRemovePeer;
  static const std::string kRaftLogEntryLockRequest;
  static const std::string kRaftLogEntryUnlockRequest;
  static const std::string kRaftLogEntryInsertRevision;
  static const std::string kRaftLogEntryRaftTransactionInfo;
  static const std::string kRaftLogEntryOther;

 private:
  friend class ConsensusFixture;
  friend class benchmarks::RaftBenchmarkTests;
  friend class benchmarks::BubbleSortTest;
  friend class RaftChunk;
  FRIEND_TEST(ConsensusFixture, LeaderElection);
  RaftNode();
  RaftNode(const RaftNode&) = delete;
  RaftNode& operator=(const RaftNode&) = delete;

  bool giveUpLeadership();

  typedef RaftChunkDataRamContainer::RaftLog::iterator LogIterator;
  typedef RaftChunkDataRamContainer::RaftLog::const_iterator ConstLogIterator;
  typedef RaftChunkDataRamContainer::LogReadAccess LogReadAccess;
  typedef RaftChunkDataRamContainer::LogWriteAccess LogWriteAccess;

  // ========
  // Handlers
  // ========
  // Raft requests.
  void handleAppendRequest(proto::AppendEntriesRequest* request,
                           const PeerId& sender, Message* response);
  void handleRequestVote(const proto::VoteRequest& request,
                         const PeerId& sender, Message* response);
  void handleQueryState(const proto::QueryState& request, Message* response);

  // Chunk Requests.
  void handleConnectRequest(const PeerId& sender,
                            proto::ConnectRequestType connect_type,
                            Message* response);
  void handleLeaveRequest(const PeerId& sender, uint64_t serial_id,
                          Message* response);
  void handleChunkLockRequest(const PeerId& sender, uint64_t serial_id,
                              Message* response);
  void handleChunkUnlockRequest(const PeerId& sender, uint64_t serial_id,
                                uint64_t lock_index, bool proceed_commits,
                                Message* response);
  void handleInsertRequest(proto::InsertRequest* request,
                           const PeerId& sender, Message* response);

  // Multi-chunk commit requests.
  void handleChunkTransactionInfo(proto::ChunkTransactionInfo* info,
                                  const PeerId& sender, Message* response);
  inline void handleQueryReadyToCommit(
      const proto::MultiChunkTransactionQuery& query, const PeerId& sender,
      Message* response);
  inline void handleCommitNotification(
      const proto::MultiChunkTransactionQuery& query, const PeerId& sender,
      Message* response);
  inline void handleAbortNotification(
      const proto::MultiChunkTransactionQuery& query, const PeerId& sender,
      Message* response);

  // Not ready if entries from older leader pending commit.
  inline bool isCommitIndexInCurrentTerm() const;

  // ====================================================
  // RPCs for heartbeat, leader election, log replication
  // ====================================================
  bool sendAppendEntries(const PeerId& peer,
                         proto::AppendEntriesRequest* append_entries,
                         proto::AppendEntriesResponse* append_response);

  enum class VoteResponse {
    VOTE_GRANTED,
    VOTE_DECLINED,
    VOTER_NOT_ELIGIBLE,
    FAILED_REQUEST
  };
  VoteResponse sendRequestVote(const PeerId& peer, uint64_t term,
                               uint64_t last_log_index, uint64_t last_log_term,
                               uint64_t current_commit_index) const;

  // Expects log write lock to have been acquired.
  bool sendInitRequest(const PeerId& peer, const LogWriteAccess& log_writer);

  uint64_t sendRejoinRequest(const PeerId& to, Message* request,
                             proto::ConnectResponse* connect_response);

  // ================
  // State Management
  // ================

  // State information.
  PeerId leader_id_;
  State state_;
  uint64_t current_term_;
  uint64_t join_log_index_;
  mutable std::mutex state_mutex_;

  // Heartbeat information.
  typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;
  mutable TimePoint last_heartbeat_;
  mutable std::mutex last_heartbeat_mutex_;
  inline void updateHeartbeatTime() const;
  inline double getTimeSinceHeartbeatMs();

  std::thread state_manager_thread_;  // Gets joined in destructor.
  std::atomic<bool> state_thread_running_;
  std::atomic<bool> is_exiting_;
  std::atomic<bool> leave_requested_;
  void stateManagerThread();

  // ===============
  // Peer management
  // ===============

  enum class PeerStatus {
    JOINING,
    AVAILABLE,
    NOT_RESPONDING,
    ANNOUNCED_DISCONNECTING,
    OFFLINE
  };

  struct FollowerTracker {
    std::thread tracker_thread;
    std::atomic<bool> tracker_run;
    std::atomic<uint64_t> replication_index;
    std::atomic<PeerStatus> status;
  };

  typedef std::unordered_map<PeerId, std::shared_ptr<FollowerTracker>> TrackerMap;
  // One tracker thread is started for each peer when leadership is acquired.
  // They get joined when leadership is lost or corresponding peer disconnects.
  TrackerMap follower_tracker_map_;

  // Available peers. Modified ONLY in followerCommitNewEntries() or
  // leaderCommitReplicatedEntries() or leaderMonitorFollowerStatus()
  std::set<PeerId> peer_list_;
  std::mutex peer_mutex_;
  std::mutex follower_tracker_mutex_;
  inline bool hasPeer(const PeerId& peer);
  inline size_t numPeers();

  // Expects follower_tracker_mutex_ locked.
  void leaderShutDownTracker(const PeerId& peer);
  void leaderShutDownAllTrackes();
  void leaderLaunchTracker(const PeerId& peer, uint64_t current_term);

  // Expects no lock to be taken.
  void leaderMonitorFollowerStatus(uint64_t current_term);

  void leaderAddPeer(const PeerId& peer, const LogWriteAccess& log_writer,
                     uint64_t current_term, bool is_rejoin_peer);
  void leaderRemovePeer(const PeerId& peer);

  void followerAddPeer(const PeerId& peer);
  void followerRemovePeer(const PeerId& peer);

  uint64_t attemptRejoin();

  // ===============
  // Leader election
  // ===============

  std::atomic<int> election_timeout_ms_;  // A random value between 50 and 150 ms.
  static int setElectionTimeout();     // Set a random election timeout value.
  void conductElection();

  std::atomic<bool> follower_trackers_run_;
  std::atomic<uint64_t> last_vote_request_term_;
  // Used only to decide to check if new log entries are available, otherwise
  // the thread sleeps for heart beat period / until new entry signal.
  // Can be inconsistent. Use LogReadAccess->lastLogIndex() to read last log
  // index.
  // and LogReadAccess->commitIndex() to read commit index.
  uint64_t last_log_index_for_follower_trackers_;
  uint64_t commit_index_for_follower_trackers_;
  std::mutex follower_tracker_wait_mutex_;
  std::condition_variable tracker_wakeup_signal_;
  void followerTrackerThread(const PeerId& peer, uint64_t term,
                             FollowerTracker* const my_tracker);

  // =====================
  // Log entries/revisions
  // =====================
  RaftChunkDataRamContainer* data_;
  void initChunkData(const proto::InitRequest& init_request);

  // Index will always be sequential, unique.
  // Leader will overwrite follower logs where index+term doesn't match.

  // Expects write lock for log_mutex to be acquired.
  uint64_t leaderAppendLogEntryLocked(
      const LogWriteAccess& log_writer,
      const std::shared_ptr<proto::RaftLogEntry>& new_entry,
      uint64_t current_term);

  // The two following methods assume write lock is acquired for log_mutex_.
  proto::AppendResponseStatus followerAppendNewEntries(
      const LogWriteAccess& log_writer,
      proto::AppendEntriesRequest* request);
  void followerCommitNewEntries(const LogWriteAccess& log_writer,
                                uint64_t request_commit_index, State state);
  inline void setAppendEntriesResponse(proto::AppendResponseStatus status,
                                uint64_t current_commit_index,
                                uint64_t current_term,
                                uint64_t last_log_index,
                                uint64_t last_log_term,
                                proto::AppendEntriesResponse* response) const;

  // Expects lock for log_mutex_to NOT have been acquired.
  void leaderCommitReplicatedEntries(uint64_t current_term);

  uint64_t getLatestFullyReplicatedEntry();

  // All three of the following are called from leader or follower commit.
  void applySingleRevisionCommit(const std::shared_ptr<proto::RaftLogEntry>& entry);
  void chunkLockEntryCommit(const LogWriteAccess& log_writer,
                            const std::shared_ptr<proto::RaftLogEntry>& entry);
  void multiChunkTransactionInfoCommit(
      const std::shared_ptr<proto::RaftLogEntry>& entry);
  void bulkApplyLockedRevisions(const LogWriteAccess& log_writer,
                                uint64_t lock_index, uint64_t unlock_index);

  // Commit Insert/update callbacks
  std::function<void(const common::Id& inserted_id)> commit_insert_callback_;
  std::function<void(const common::Id& inserted_id)> commit_update_callback_;
  std::function<void(void)> commit_unlock_callback_;

  std::condition_variable entry_replicated_signal_;
  std::condition_variable entry_committed_signal_;

  std::unique_ptr<MultiChunkTransaction> multi_chunk_transaction_manager_;
  void initializeMultiChunkTransactionManager();
  void manageIncompleteTransaction(const LogWriteAccess& log_writer,
                                   const PeerId& peer, uint64_t current_term);

  class DistributedRaftChunkLock {
   public:
    DistributedRaftChunkLock()
        : holder_(PeerId()),
          is_locked_(false),
          lock_entry_index_(0) {}
    bool writeLock(const PeerId& peer, uint64_t index);
    bool unlock();
    uint64_t lock_entry_index() const;
    bool isLocked() const;
    const PeerId& holder() const;
    bool isLockHolder(const PeerId& peer) const;

   private:
    PeerId holder_;
    bool is_locked_;
    uint64_t lock_entry_index_;
    mutable std::mutex mutex_;
  };
  DistributedRaftChunkLock raft_chunk_lock_;
  std::mutex chunk_lock_mutex_;

  // Not protected by a mutex because this is only accessed from
  // follower/leader commit functions.
  std::queue<PeerId> lock_queue_;
  inline void grantChunkLockFromQueue(const LogWriteAccess& log_writer,
                                      const uint64_t current_term);

  // Raft Chunk Requests.
  uint64_t sendChunkLockRequest(uint64_t serial_id);
  bool sendChunkUnlockRequest(uint64_t serial_id, uint64_t lock_index,
                                  bool proceed_commits);
  bool sendChunkTransactionInfo(proto::ChunkTransactionInfo* info,
                                uint64_t serial_id);
  // New revision request.
  bool sendInsertRequest(const Revision::ConstPtr& item, uint64_t serial_id);

  bool waitAndCheckCommit(uint64_t index, uint64_t append_term,
                          uint64_t serial_id);

  bool sendLeaveRequest(uint64_t serial_id);
  void sendLeaveSuccessNotification(const PeerId& peer);

  void processChunkLockRequest(const PeerId& sender, uint64_t serial_id,
                               proto::RaftChunkRequestResponse* response);
  void processChunkUnlockRequest(const PeerId& sender, uint64_t serial_id,
                                 uint64_t lock_index, uint64_t proceed_commits,
                                 proto::RaftChunkRequestResponse* response);
  void processChunkTransactionInfo(
      const PeerId& sender, uint64_t serial_id, uint64_t num_entries,
      proto::MultiChunkTransactionInfo* unowned_multi_chunk_info_ptr,
      proto::RaftChunkRequestResponse* response);
  void processInsertRequest(const PeerId& sender, uint64_t serial_id,
                            proto::Revision* unowned_revision_pointer,
                            proto::RaftChunkRequestResponse* response);
  void processLeaveRequest(const PeerId& sender, uint64_t serial_id,
                           proto::RaftChunkRequestResponse* response);

  inline const std::string getLogEntryTypeString(
      const std::shared_ptr<proto::RaftLogEntry>& entry) const;

  // ========================
  // Owner chunk information.
  // ========================
  std::string table_name_;
  common::Id chunk_id_;
  template <typename RequestType>
  inline void fillMetadata(RequestType* destination) const;

  // ==================
  // Hooks for testing.
  // ==================
  std::function<void(const uint64_t term)> lost_leadership_callback_;
  std::function<void(const uint64_t term)> elected_as_leader_callback_;
  std::function<void(void)> new_leader_found_callback_;
  std::function<void(const uint64_t index, const std::string& entry_type)>
      leader_entry_appended_callback_;
  std::function<void(const uint64_t index, const std::string& entry_type)>
      leader_entry_committed_callback_;
  std::function<void(const PeerId& peer)> peer_disconnection_detected_callback_;
};

}  // namespace map_api

#include "./raft-node-inl.h"

#endif  // MAP_API_RAFT_NODE_H_
