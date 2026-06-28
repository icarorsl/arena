#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <map>
#include <shared_mutex>

#include "common/types.h"

namespace filegroup {

/**
 * Raft consensus for file group registry.
 * Maintains state of all files and their versions in the group.
 * Phase 1: Single-node implementation (leader-only, no replication).
 * Phase 2+: Will add followers and true consensus.
 */

// Raft log entry types
enum class RaftEntryType : uint8_t {
    NOP = 0x00,              // No-op entry
    SNAPSHOT_COMPLETE = 0x01, // Snapshot checkpoint
    UNKNOWN = 0xFF,
};

#pragma pack(push, 1)
struct RaftLogEntry {
    uint64_t term;            // Raft term (8 bytes)
    uint64_t index;           // Log index (8 bytes)
    uint16_t data_length;     // Length of appended data (2 bytes)
    RaftEntryType type;       // Entry type (1 byte)
    uint8_t padding[5];       // Align to 24 bytes
};
#pragma pack(pop)

/**
 * In-memory Raft state.
 * Tracks log, election state, commit/apply indices.
 */
struct RaftState {
    uint64_t current_term;
    uint64_t voted_for;           // Node that received vote in current_term (0 = none)
    uint64_t commit_index;        // Highest log index known to be committed
    uint64_t last_applied_index;  // Highest log index applied to file index
    std::vector<RaftLogEntry> log;
};

/**
 * Raft registry manager.
 * Handles Raft consensus, log persistence, and applying entries to file index.
 * Phase 1: Single-node leader (no followers, no true consensus).
 * Transitions entries from manifest to Raft log via replication.
 */
class RaftRegistry {
public:
    /**
     * Create or open Raft registry.
     * @param log_path path to raft log file
     * @param local_node_id node ID of this server
     * @param group_id file group ID
     */
    explicit RaftRegistry(const std::string& log_path, uint16_t local_node_id, uint32_t group_id);

    ~RaftRegistry();

    /**
     * Append an entry to the Raft log (leader only).
     * @param entry_data pointer to entry data (type-specific)
     * @param entry_length length of entry data
     * @return log index of appended entry
     */
    uint64_t append_entry(const void* entry_data, uint16_t entry_length);

    /**
     * Commit entries up to given index (leader).
     * Advances commit_index and applies to file index.
     */
    void commit_up_to(uint64_t index);

    /**
     * Get Raft state (current term, voted_for, indices).
     */
    const RaftState& get_state() const { return state_; }

    /**
     * Get next log index to append.
     */
    uint64_t next_log_index() const;

    /**
     * Get last log term.
     */
    uint64_t last_log_term() const;

private:
    std::string log_path_;
    uint16_t local_node_id_;
    uint32_t group_id_;
    RaftState state_;
    mutable std::shared_mutex state_mutex_;
};

}  // namespace filegroup
