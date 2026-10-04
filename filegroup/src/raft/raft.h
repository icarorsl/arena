#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/types.h"
#include "manifest/manifest.h"

namespace filegroup {

// ============================================================================
// Raft Core Types
// ============================================================================

enum class RaftRole : uint8_t {
    FOLLOWER = 0,
    CANDIDATE = 1,
    LEADER = 2,
};

// A single entry in the Raft log. The `data` field is the serialized manifest entry.
struct RaftLogEntry {
    uint64_t term;     // Term when entry was received by leader
    uint64_t index;    // Log index (1-based, monotonically increasing)
    uint32_t entry_type; // ManifestEntryType enum value
    std::vector<uint8_t> data; // Serialized manifest entry body

    RaftLogEntry() : term(0), index(0), entry_type(0) {}
    RaftLogEntry(uint64_t t, uint64_t i, uint32_t et, std::vector<uint8_t> d)
        : term(t), index(i), entry_type(et), data(std::move(d)) {}
};

// Persistent Raft state (survives restarts)
struct PersistentState {
    uint64_t current_term = 1;
    uint32_t voted_for = 0;      // Node ID voted for in current term, 0 = none
    std::vector<RaftLogEntry> log;
};

// Volatile state on all nodes
struct VolatileState {
    uint64_t commit_index = 0;
    uint64_t last_applied = 0;
};

// Volatile state on leader only
struct LeaderState {
    // For each follower: next index to send
    std::unordered_map<uint32_t, uint64_t> next_index;
    // For each follower: highest log index known to be replicated
    std::unordered_map<uint32_t, uint64_t> match_index;
};

// ============================================================================
// Raft RPC Messages (abstract — can be in-process or gRPC)
// ============================================================================

struct RequestVoteArgs {
    uint64_t term;
    uint32_t candidate_id;
    uint64_t last_log_index;
    uint64_t last_log_term;
};

struct RequestVoteReply {
    uint64_t term;
    bool vote_granted;
};

struct AppendEntriesArgs {
    uint64_t term;
    uint32_t leader_id;
    uint64_t prev_log_index;
    uint64_t prev_log_term;
    std::vector<RaftLogEntry> entries;
    uint64_t leader_commit;
};

struct AppendEntriesReply {
    uint64_t term;
    bool success;
    uint64_t match_index; // Highest log index replicated on this follower
};

// ============================================================================
// RaftTransport — Abstract interface for inter-node RPC
// ============================================================================

class RaftTransport {
public:
    virtual ~RaftTransport() = default;
    virtual RequestVoteReply send_request_vote(uint32_t peer_id, const RequestVoteArgs& args) = 0;
    virtual AppendEntriesReply send_append_entries(uint32_t peer_id, const AppendEntriesArgs& args) = 0;
};

// ============================================================================
// RaftConfig
// ============================================================================

struct RaftConfig {
    uint32_t local_node_id;
    std::vector<uint32_t> peer_node_ids; // All cluster members (including self)

    // Timing (microseconds)
    uint64_t election_timeout_min_us = 150'000;   // 150ms
    uint64_t election_timeout_max_us = 300'000;   // 300ms
    uint64_t heartbeat_interval_us = 50'000;      // 50ms

    // Log persistence path
    std::string log_path;
    uint32_t group_id = 0;
};

// ============================================================================
// RaftNode — Core Raft consensus implementation
// ============================================================================

class RaftNode {
public:
    // Callback type: called when entries are committed and should be applied to the state machine
    // Parameters: entry_type, entry_body_data, body_length
    using ApplyCallback = std::function<void(uint32_t entry_type, const void* body, uint16_t body_length, uint64_t lsn)>;

    RaftNode(RaftConfig config,
             std::unique_ptr<RaftTransport> transport,
             ApplyCallback apply_callback);
    ~RaftNode();

    // ---- Public API ----

    /// Propose an entry to the Raft log (callable from any node; only leader accepts).
    /// Returns (success, log_index). On non-leader, returns (false, 0).
    std::pair<bool, uint64_t> propose(uint32_t entry_type, const void* body, uint16_t body_length);

    /// Get current role.
    RaftRole role() const;

    /// Get current leader (node_id), or 0 if unknown.
    uint32_t leader_id() const;

    /// Get current term.
    uint64_t current_term() const;

    /// Get commit index.
    uint64_t commit_index() const;

    /// Get log size.
    size_t log_size() const;

    /// Check if this node is the leader.
    bool is_leader() const;

    /// Get the cluster member node IDs.
    const std::vector<uint32_t>& cluster_nodes() const;

    /// Register a peer node for in-process transport (test/debug only).
    void register_inprocess_peer(uint32_t peer_id, RaftNode* peer_node);

    /// RequestVote RPC handler (called by RaftTransport on incoming vote request).
    RequestVoteReply handle_request_vote(const RequestVoteArgs& args);

    /// AppendEntries RPC handler (called by RaftTransport on incoming append entries).
    AppendEntriesReply handle_append_entries(const AppendEntriesArgs& args);

private:
    // ---- Event loop ----
    void run();
    void stop();

    // ---- State transitions ----
    void become_follower(uint64_t term);
    void become_candidate();
    void become_leader();

    // ---- Election ----
    void start_election();
    void reset_election_timer();

    // ---- Leader operations ----
    void send_heartbeats();
    void replicate_log();
    void advance_commit_index();

    // ---- Log operations ----
    void persist_state();
    void restore_state();
    void apply_committed_entries();

    // ---- Helpers ----
    uint64_t randomized_timeout() const;
    uint64_t last_log_index() const;
    uint64_t last_log_term() const;
    RaftLogEntry* get_entry(uint64_t index);
    const RaftLogEntry* get_entry(uint64_t index) const;

    // Configuration
    RaftConfig config_;
    std::unique_ptr<RaftTransport> transport_;
    ApplyCallback apply_callback_;

    // Persistent state
    PersistentState persistent_;
    std::mutex persistent_mutex_;

    // Volatile state
    VolatileState volatile_;
    LeaderState leader_state_;

    // Role and leadership
    std::atomic<RaftRole> role_{RaftRole::FOLLOWER};
    std::atomic<uint32_t> leader_id_{0};

    // Election
    std::atomic<uint64_t> election_deadline_us_{0};
    std::atomic<uint32_t> votes_received_{0};
    std::mutex election_mutex_;
    std::random_device rd_;
    std::mt19937 rng_;

    // Threading
    std::thread event_thread_;
    std::atomic<bool> running_{false};
    std::condition_variable cv_;
    std::mutex cv_mutex_;

    // For heartbeats and log replication triggers
    std::atomic<uint64_t> last_heartbeat_us_{0};
    std::atomic<bool> has_new_entries_{false};
};

// ============================================================================
// InProcessRaftTransport — For testing with threads, no network
// ============================================================================

class InProcessRaftTransport : public RaftTransport {
public:
    void register_node(uint32_t node_id, RaftNode* node);
    RequestVoteReply send_request_vote(uint32_t peer_id, const RequestVoteArgs& args) override;
    AppendEntriesReply send_append_entries(uint32_t peer_id, const AppendEntriesArgs& args) override;

private:
    std::mutex mutex_;
    std::unordered_map<uint32_t, RaftNode*> nodes_;
};

}  // namespace filegroup
