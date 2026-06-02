#include "raft/raft.h"

#include "common/clock.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace filegroup {

// ============================================================================
// RaftNode — Constructor / Destructor
// ============================================================================

RaftNode::RaftNode(RaftConfig config,
                   std::unique_ptr<RaftTransport> transport,
                   ApplyCallback apply_callback)
    : config_(std::move(config))
    , transport_(std::move(transport))
    , apply_callback_(std::move(apply_callback))
    , rng_(rd_())
{
    if (config_.local_node_id == 0) {
        throw std::invalid_argument("local_node_id must be non-zero");
    }
    if (config_.peer_node_ids.empty()) {
        throw std::invalid_argument("peer_node_ids must not be empty");
    }

    // Restore persistent state from disk
    restore_state();
    reset_election_timer();

    // Start event loop
    running_ = true;
    event_thread_ = std::thread(&RaftNode::run, this);
}

RaftNode::~RaftNode() {
    stop();
}

void RaftNode::stop() {
    running_ = false;
    cv_.notify_all();
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
}

// ============================================================================
// Public API
// ============================================================================

std::pair<bool, uint64_t> RaftNode::propose(uint32_t entry_type, const void* body, uint16_t body_length) {
    std::lock_guard<std::mutex> lock(persistent_mutex_);

    if (role_ != RaftRole::LEADER) {
        return {false, 0};
    }

    RaftLogEntry entry;
    entry.term = persistent_.current_term;
    entry.index = persistent_.log.empty() ? 1 : persistent_.log.back().index + 1;
    entry.entry_type = entry_type;
    entry.data.assign(static_cast<const uint8_t*>(body),
                      static_cast<const uint8_t*>(body) + body_length);

    persistent_.log.push_back(entry);

    // Leader's own match_index is always up to date
    leader_state_.match_index[config_.local_node_id] = entry.index;

    // Signal replication thread
    has_new_entries_ = true;
    cv_.notify_all();

    persist_state();

    return {true, entry.index};
}

RaftRole RaftNode::role() const { return role_.load(); }
uint32_t RaftNode::leader_id() const { return leader_id_.load(); }
uint64_t RaftNode::current_term() const { return persistent_.current_term; }
uint64_t RaftNode::commit_index() const { return volatile_.commit_index; }
size_t RaftNode::log_size() const { return persistent_.log.size(); }
bool RaftNode::is_leader() const { return role_ == RaftRole::LEADER; }

const std::vector<uint32_t>& RaftNode::cluster_nodes() const {
    return config_.peer_node_ids;
}

void RaftNode::register_inprocess_peer(uint32_t peer_id, RaftNode* peer_node) {
    auto* transport = dynamic_cast<InProcessRaftTransport*>(transport_.get());
    if (transport) {
        transport->register_node(peer_id, peer_node);
    }
}

// ============================================================================
// State Transitions
// ============================================================================

void RaftNode::become_follower(uint64_t term) {
    role_ = RaftRole::FOLLOWER;
    persistent_.current_term = term;
    persistent_.voted_for = 0;
    leader_id_ = 0;
    persist_state();
}

void RaftNode::become_candidate() {
    role_ = RaftRole::CANDIDATE;
    persistent_.current_term++;
    persistent_.voted_for = config_.local_node_id;
    votes_received_ = 1; // Vote for self
    leader_id_ = 0;
    persist_state();
}

void RaftNode::become_leader() {
    role_ = RaftRole::LEADER;
    leader_id_ = config_.local_node_id;

    // Initialize leader state
    uint64_t next_idx = persistent_.log.empty() ? 1 : persistent_.log.back().index + 1;
    for (uint32_t peer : config_.peer_node_ids) {
        leader_state_.next_index[peer] = next_idx;
        leader_state_.match_index[peer] = 0;
    }
    leader_state_.match_index[config_.local_node_id] = last_log_index();

    // Send immediate heartbeats to assert leadership
    last_heartbeat_us_ = 0; // Force immediate heartbeat
    cv_.notify_all();

    std::cout << "[raft] node " << config_.local_node_id
              << " became LEADER for term " << persistent_.current_term << std::endl;
}

// ============================================================================
// Event Loop
// ============================================================================

void RaftNode::run() {
    while (running_) {
        uint64_t now = now_us_monotonic();

        if (role_ == RaftRole::LEADER) {
            // Leader: send heartbeats periodically
            if (now - last_heartbeat_us_ >= config_.heartbeat_interval_us || has_new_entries_) {
                send_heartbeats();
                last_heartbeat_us_ = now;
                has_new_entries_ = false;
            }
        } else {
            // Follower / Candidate: check election timeout
            if (now >= election_deadline_us_) {
                start_election();
            }
        }

        // Apply committed entries
        apply_committed_entries();

        // Sleep until next event
        uint64_t sleep_us;
        if (role_ == RaftRole::LEADER) {
            sleep_us = config_.heartbeat_interval_us;
        } else {
            uint64_t deadline = election_deadline_us_;
            sleep_us = (deadline > now) ? (deadline - now) : config_.heartbeat_interval_us / 2;
        }
        sleep_us = std::min(sleep_us, uint64_t(50'000)); // Max 50ms sleep

        std::unique_lock<std::mutex> lk(cv_mutex_);
        cv_.wait_for(lk, std::chrono::microseconds(sleep_us));
    }
}

// ============================================================================
// Election
// ============================================================================

void RaftNode::start_election() {
    if (role_ == RaftRole::LEADER) return;

    become_candidate();
    reset_election_timer();

    std::cout << "[raft] node " << config_.local_node_id
              << " starting election for term " << persistent_.current_term << std::endl;

    RequestVoteArgs args;
    args.term = persistent_.current_term;
    args.candidate_id = config_.local_node_id;
    args.last_log_index = last_log_index();
    args.last_log_term = last_log_term();

    for (uint32_t peer : config_.peer_node_ids) {
        if (peer == config_.local_node_id) continue;

        // Send RequestVote asynchronously (fire and forget)
        // In production this would be async; for Phase 1 we do it in-thread for simplicity
        RequestVoteReply reply = transport_->send_request_vote(peer, args);

        std::lock_guard<std::mutex> lock(election_mutex_);

        if (reply.term > persistent_.current_term) {
            become_follower(reply.term);
            reset_election_timer();
            return;
        }

        if (reply.vote_granted && role_ == RaftRole::CANDIDATE
            && reply.term == persistent_.current_term) {
            votes_received_++;
        }
    }

    // Check if won election
    uint32_t majority = config_.peer_node_ids.size() / 2 + 1;
    if (votes_received_ >= majority && role_ == RaftRole::CANDIDATE) {
        become_leader();
    }
}

void RaftNode::reset_election_timer() {
    election_deadline_us_ = now_us_monotonic() + randomized_timeout();
}

uint64_t RaftNode::randomized_timeout() const {
    std::uniform_int_distribution<uint64_t> dist(
        config_.election_timeout_min_us,
        config_.election_timeout_max_us);
    // Use a local RNG for thread safety (rng_ is not locked here, but this is only called
    // from the event loop thread)
    return dist(const_cast<std::mt19937&>(rng_));
}

// ============================================================================
// RequestVote RPC Handler
// ============================================================================

RequestVoteReply RaftNode::handle_request_vote(const RequestVoteArgs& args) {
    std::lock_guard<std::mutex> lock(persistent_mutex_);

    RequestVoteReply reply;
    reply.term = persistent_.current_term;

    // Reject if candidate's term is behind
    if (args.term < persistent_.current_term) {
        reply.vote_granted = false;
        return reply;
    }

    // If candidate's term is higher, step down
    if (args.term > persistent_.current_term) {
        become_follower(args.term);
        reply.term = persistent_.current_term;
    }

    // Check if we already voted for someone else in this term
    bool can_vote = (persistent_.voted_for == 0 || persistent_.voted_for == args.candidate_id);

    // Check if candidate's log is at least as up-to-date as ours
    bool log_ok = false;
    uint64_t my_last_term = last_log_term();
    if (args.last_log_term > my_last_term) {
        log_ok = true;
    } else if (args.last_log_term == my_last_term && args.last_log_index >= last_log_index()) {
        log_ok = true;
    }

    if (can_vote && log_ok) {
        persistent_.voted_for = args.candidate_id;
        reply.vote_granted = true;
        persist_state();
        reset_election_timer(); // Granted vote, reset our election timer
    } else {
        reply.vote_granted = false;
    }

    return reply;
}

// ============================================================================
// Leader: Heartbeats + Log Replication
// ============================================================================

void RaftNode::send_heartbeats() {
    if (role_ != RaftRole::LEADER) return;

    AppendEntriesArgs args;
    args.term = persistent_.current_term;
    args.leader_id = config_.local_node_id;
    args.leader_commit = volatile_.commit_index;

    for (uint32_t peer : config_.peer_node_ids) {
        if (peer == config_.local_node_id) continue;

        // Build per-follower args
        uint64_t next_idx = leader_state_.next_index[peer];
        args.prev_log_index = next_idx - 1;
        args.prev_log_term = 0;

        // Determine prev_log_term
        if (args.prev_log_index > 0 && args.prev_log_index <= persistent_.log.size()) {
            args.prev_log_term = persistent_.log[args.prev_log_index - 1].term;
        }

        // Gather entries to send
        args.entries.clear();
        if (next_idx <= persistent_.log.size()) {
            for (size_t i = next_idx - 1; i < persistent_.log.size(); i++) {
                args.entries.push_back(persistent_.log[i]);
            }
        }

        AppendEntriesReply reply = transport_->send_append_entries(peer, args);

        std::lock_guard<std::mutex> lock(persistent_mutex_);

        if (reply.term > persistent_.current_term) {
            become_follower(reply.term);
            reset_election_timer();
            return;
        }

        if (reply.success) {
            leader_state_.match_index[peer] = reply.match_index;
            leader_state_.next_index[peer] = reply.match_index + 1;
        } else {
            // Decrement next_index and retry next heartbeat
            if (leader_state_.next_index[peer] > 1) {
                leader_state_.next_index[peer]--;
            }
        }
    }

    // Advance commit index
    advance_commit_index();
}

void RaftNode::advance_commit_index() {
    // Find the highest log index replicated on a majority of nodes
    std::vector<uint64_t> match_indices;
    for (auto& [node_id, idx] : leader_state_.match_index) {
        match_indices.push_back(idx);
    }
    std::sort(match_indices.begin(), match_indices.end(), std::greater<uint64_t>());

    uint32_t majority = config_.peer_node_ids.size() / 2 + 1;
    if (match_indices.size() >= majority) {
        uint64_t new_commit = match_indices[majority - 1];
        // Only commit entries from current term (Raft safety property)
        if (new_commit > volatile_.commit_index) {
            // Check term of entry at new_commit
            if (new_commit <= persistent_.log.size()) {
                uint64_t entry_term = persistent_.log[new_commit - 1].term;
                if (entry_term == persistent_.current_term) {
                    volatile_.commit_index = new_commit;
                }
            }
        }
    }
}

// ============================================================================
// AppendEntries RPC Handler
// ============================================================================

AppendEntriesReply RaftNode::handle_append_entries(const AppendEntriesArgs& args) {
    std::lock_guard<std::mutex> lock(persistent_mutex_);

    AppendEntriesReply reply;
    reply.term = persistent_.current_term;
    reply.success = false;

    // Reject if leader's term is behind
    if (args.term < persistent_.current_term) {
        reply.match_index = 0;
        return reply;
    }

    // Valid leader: step down if we thought we were leader/candidate
    if (args.term >= persistent_.current_term) {
        if (role_ != RaftRole::FOLLOWER) {
            become_follower(args.term);
        }
        persistent_.current_term = args.term;
        leader_id_ = args.leader_id;
        persist_state();
    }

    reset_election_timer(); // Valid leader heartbeat received

    // Check prev_log_index consistency
    if (args.prev_log_index > 0) {
        if (args.prev_log_index > persistent_.log.size()) {
            reply.match_index = persistent_.log.size();
            return reply;
        }
        if (persistent_.log[args.prev_log_index - 1].term != args.prev_log_term) {
            // Conflict: delete this entry and all that follow
            persistent_.log.erase(persistent_.log.begin() + (args.prev_log_index - 1),
                                  persistent_.log.end());
            reply.match_index = args.prev_log_index - 1;
            persist_state();
            return reply;
        }
    }

    // Append new entries (skip existing matching entries)
    for (size_t i = 0; i < args.entries.size(); i++) {
        const auto& entry = args.entries[i];
        uint64_t idx = args.prev_log_index + 1 + i;

        if (idx <= persistent_.log.size()) {
            // Existing entry — if term conflicts, delete from here onward
            if (persistent_.log[idx - 1].term != entry.term) {
                persistent_.log.erase(persistent_.log.begin() + (idx - 1),
                                      persistent_.log.end());
                persistent_.log.push_back(entry);
            }
            // else: entry already matches, keep it
        } else {
            persistent_.log.push_back(entry);
        }
    }

    // Update commit index
    if (args.leader_commit > volatile_.commit_index) {
        volatile_.commit_index = std::min(args.leader_commit, last_log_index());
    }

    reply.success = true;
    reply.match_index = last_log_index();
    reply.term = persistent_.current_term;

    // Notify event loop to apply committed entries
    cv_.notify_all();

    return reply;
}

// ============================================================================
// Apply Committed Entries
// ============================================================================

void RaftNode::apply_committed_entries() {
    while (volatile_.last_applied < volatile_.commit_index) {
        uint64_t next_idx = volatile_.last_applied + 1;
        if (next_idx > persistent_.log.size()) break;

        const auto& entry = persistent_.log[next_idx - 1];

        if (apply_callback_) {
            apply_callback_(entry.entry_type, entry.data.data(),
                           static_cast<uint16_t>(entry.data.size()), entry.index);
        }

        volatile_.last_applied = next_idx;
    }
}

// ============================================================================
// Log Helpers
// ============================================================================

uint64_t RaftNode::last_log_index() const {
    return persistent_.log.empty() ? 0 : persistent_.log.back().index;
}

uint64_t RaftNode::last_log_term() const {
    return persistent_.log.empty() ? 0 : persistent_.log.back().term;
}

RaftLogEntry* RaftNode::get_entry(uint64_t index) {
    if (index == 0 || index > persistent_.log.size()) return nullptr;
    return &persistent_.log[index - 1];
}

const RaftLogEntry* RaftNode::get_entry(uint64_t index) const {
    if (index == 0 || index > persistent_.log.size()) return nullptr;
    return &persistent_.log[index - 1];
}

// ============================================================================
// Persistence
// ============================================================================

void RaftNode::persist_state() {
    if (config_.log_path.empty()) return;

    // Simple binary format:
    // [uint64_t current_term][uint32_t voted_for][uint64_t entry_count]
    // Then for each entry: [uint64_t term][uint64_t index][uint32_t entry_type][uint32_t data_len][data bytes]

    std::ofstream file(config_.log_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "[raft] Failed to persist state to " << config_.log_path << std::endl;
        return;
    }

    file.write(reinterpret_cast<const char*>(&persistent_.current_term), sizeof(uint64_t));
    file.write(reinterpret_cast<const char*>(&persistent_.voted_for), sizeof(uint32_t));

    uint64_t count = persistent_.log.size();
    file.write(reinterpret_cast<const char*>(&count), sizeof(uint64_t));

    for (const auto& entry : persistent_.log) {
        file.write(reinterpret_cast<const char*>(&entry.term), sizeof(uint64_t));
        file.write(reinterpret_cast<const char*>(&entry.index), sizeof(uint64_t));
        file.write(reinterpret_cast<const char*>(&entry.entry_type), sizeof(uint32_t));
        uint32_t data_len = static_cast<uint32_t>(entry.data.size());
        file.write(reinterpret_cast<const char*>(&data_len), sizeof(uint32_t));
        if (data_len > 0) {
            file.write(reinterpret_cast<const char*>(entry.data.data()), data_len);
        }
    }

    file.close();
}

void RaftNode::restore_state() {
    if (config_.log_path.empty()) return;

    std::ifstream file(config_.log_path, std::ios::binary);
    if (!file.is_open()) return; // No persisted state yet

    file.read(reinterpret_cast<char*>(&persistent_.current_term), sizeof(uint64_t));
    file.read(reinterpret_cast<char*>(&persistent_.voted_for), sizeof(uint32_t));

    uint64_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(uint64_t));

    if (file.fail()) {
        // Corrupt or incomplete file — start fresh
        persistent_.current_term = 1;
        persistent_.voted_for = 0;
        persistent_.log.clear();
        return;
    }

    persistent_.log.reserve(count);
    for (uint64_t i = 0; i < count; i++) {
        RaftLogEntry entry;
        file.read(reinterpret_cast<char*>(&entry.term), sizeof(uint64_t));
        file.read(reinterpret_cast<char*>(&entry.index), sizeof(uint64_t));
        file.read(reinterpret_cast<char*>(&entry.entry_type), sizeof(uint32_t));

        uint32_t data_len = 0;
        file.read(reinterpret_cast<char*>(&data_len), sizeof(uint32_t));

        if (data_len > 0) {
            entry.data.resize(data_len);
            file.read(reinterpret_cast<char*>(entry.data.data()), data_len);
        }

        if (!file.fail()) {
            persistent_.log.push_back(std::move(entry));
        }
    }

    // Restore commit/last_applied to log size (safe — replay will catch up)
    volatile_.commit_index = persistent_.log.size();
    volatile_.last_applied = persistent_.log.size();

    std::cout << "[raft] node " << config_.local_node_id
              << " restored " << persistent_.log.size()
              << " entries, term=" << persistent_.current_term << std::endl;
}

// ============================================================================
// InProcessRaftTransport
// ============================================================================

void InProcessRaftTransport::register_node(uint32_t node_id, RaftNode* node) {
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_[node_id] = node;
}

RequestVoteReply InProcessRaftTransport::send_request_vote(uint32_t peer_id, const RequestVoteArgs& args) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(peer_id);
    if (it == nodes_.end()) {
        RequestVoteReply reply;
        reply.term = 0;
        reply.vote_granted = false;
        return reply;
    }
    return it->second->handle_request_vote(args);
}

AppendEntriesReply InProcessRaftTransport::send_append_entries(uint32_t peer_id, const AppendEntriesArgs& args) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(peer_id);
    if (it == nodes_.end()) {
        AppendEntriesReply reply;
        reply.term = 0;
        reply.success = false;
        reply.match_index = 0;
        return reply;
    }
    return it->second->handle_append_entries(args);
}

}  // namespace filegroup
