#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/types.h"
#include "manifest/file_index.h"
#include "raft/raft.h"

namespace filegroup {

// ============================================================================
// RegistryServer — In-process registry with Raft-backed FileIndex
// Phase 1: In-process transport. Phase 2+: gRPC wrapper around this.
// ============================================================================

class RegistryServer {
public:
    RegistryServer(uint32_t node_id,
                   const std::vector<uint32_t>& peer_ids,
                   const std::string& raft_log_path,
                   uint32_t group_id);
    ~RegistryServer();

    /// Start the Raft node (begins election / following).
    void start();

    /// Stop the server.
    void stop();

    /// Get the file index (shared, thread-safe).
    FileIndex& file_index();
    const FileIndex& file_index() const;

    /// Get the Raft node.
    RaftNode& raft_node();

    /// Get this node's ID.
    uint32_t node_id() const;

    /// Check if this node is the Raft leader.
    bool is_leader() const;

    /// Get the current leader's node ID (0 if unknown).
    uint32_t leader_id() const;

    /// Block until this node becomes leader or timeout (microseconds).
    /// Returns true if became leader, false on timeout.
    bool wait_for_leader(uint64_t timeout_us = 5'000'000);

private:
    uint32_t node_id_;
    uint32_t group_id_;
    FileIndex file_index_;
    std::unique_ptr<RaftNode> raft_;
    std::unique_ptr<InProcessRaftTransport> transport_;
};

// ============================================================================
// RegistryClient — Engine-side client for registry communication
// Phase 1: Direct reference to server. Phase 2+: gRPC.
// ============================================================================

class RegistryClient {
public:
    /// Create a client connected to multiple registry servers (for leader discovery).
    /// The client tries each server until it finds the leader.
    explicit RegistryClient(std::vector<RegistryServer*> servers);

    /// Append a manifest entry. Finds the leader, sends proposal, waits for commit.
    /// Returns (success, lsn).
    std::pair<bool, uint64_t> append_entry(
        uint32_t entry_type, const void* body, uint16_t body_length);

    /// Get file metadata from the file index.
    const LogicalFileEntry* get_file(uint64_t logical_file_id);

    /// Get latest complete version.
    const VersionEntry* get_latest_complete(uint64_t logical_file_id);

    /// Get a specific version.
    const VersionEntry* get_version(uint64_t logical_file_id, uint32_t version);

    /// List files by group/table.
    std::vector<LogicalFileEntry> list_files(uint16_t table_id, uint32_t group_id);

    /// Return all files (for compaction/scrubbing).
    std::vector<LogicalFileEntry> all_files();

    /// Get confirmed chunks for a session.
    std::vector<uint32_t> get_confirmed_chunks(uint64_t session_id);

    /// Report node health.
    void report_node_health(uint16_t node_id, NodeState state);

    /// Get cluster health.
    std::unordered_map<uint16_t, NodeState> get_cluster_health();

    /// Table management.
    std::vector<TableEntry> get_tables();

    /// Find the current leader server.
    RegistryServer* find_leader();

    /// Generate unique IDs.
    uint64_t next_logical_file_id();
    uint64_t next_file_id();
    uint64_t next_session_id();

private:
    std::vector<RegistryServer*> servers_;
};

}  // namespace filegroup
