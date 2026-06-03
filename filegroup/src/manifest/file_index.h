#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>

#include "common/types.h"
#include "manifest/manifest.h"

namespace filegroup {

// Replica location in storage
struct ReplicaLocation {
    uint16_t node_id;
    std::string segment_file;
    uint64_t offset;
    ReplicaState state;
    NodeRole role;                // Always ORIGIN in Phase 1
    std::string region;           // Empty in Phase 1
    uint16_t projection_id;       // Always 0 in Phase 1
};

// Chunk location with replicas
struct ChunkLocation {
    uint32_t chunk_index;
    uint64_t chunk_size_actual;
    uint32_t chunk_checksum;
    std::vector<ReplicaLocation> replicas;
};

// Version entry (all versions of a file)
struct VersionEntry {
    uint64_t file_id;
    uint32_t version_number;
    VersionState state;
    uint64_t expires_at;
    uint64_t total_size;
    uint32_t chunk_count;
    uint64_t chunk_size;
    uint8_t replication_factor;
    EncryptionAlgo encryption;
    uint32_t content_checksum;
    uint64_t upload_session_id;
    uint64_t created_at_us;
    SegmentType segment_type;
    std::string page_bucket;
    std::vector<ChunkLocation> chunks;
    // Phase 3 projections (empty in Phase 1)
    // std::map<uint16_t, ProjectionEntry> projections;
};

// Simple table entry for dynamic table creation
struct TableEntry {
    uint32_t table_id;
    uint32_t group_id;
    std::string name;
    uint64_t chunk_size;
    uint8_t replication_factor;
    EncryptionAlgo encryption;
    uint32_t max_versions;
    uint32_t file_expires_in_days;
    ExpiryGranularity expiry_granularity;
};

// Logical file entry (all versions)
struct LogicalFileEntry {
    uint64_t logical_file_id;
    uint16_t table_id;
    uint32_t group_id;
    uint32_t latest_complete_version;
    uint32_t next_version_number;
    std::map<uint32_t, VersionEntry> versions;  // by version number
};

/**
 * In-memory file index — state from manifest replay.
 * Thread-safe with shared_mutex.
 */
class FileIndex {
public:
    FileIndex();

    // Apply manifest entries during replay
    void apply_session_open(const SessionOpenEntry& e);
    void apply_chunk_confirmed(const ChunkConfirmedEntry& e);
    void apply_version_complete(const VersionCompleteEntry& e);
    void apply_version_deleted(const VersionDeletedEntry& e);
    void apply_file_deleted(const FileDeletedEntry& e);
    void apply_session_timed_out(const SessionTimedOutEntry& e);
    void apply_chunk_delete_confirmed(const ChunkDeleteConfirmedEntry& e);
    void apply_page_deleted(const PageDeletedEntry& e);
    void apply_node_health(const NodeHealthEntry& e);
    void apply_max_versions_enforced(const MaxVersionsEnforcedEntry& e);
    void apply_table_created(const TableCreatedEntry& e);

    // Query methods
    const LogicalFileEntry* get_file(uint64_t logical_file_id) const;
    const VersionEntry* get_latest_complete(uint64_t logical_file_id) const;
    const VersionEntry* get_version(uint64_t logical_file_id, uint32_t version) const;
    std::vector<uint32_t> get_confirmed_chunks(uint64_t session_id) const;
    bool is_chunk_confirmed(uint64_t session_id, uint32_t chunk_index) const;
    std::vector<LogicalFileEntry> list_files(uint16_t table_id, uint32_t group_id) const;
    std::vector<LogicalFileEntry> all_files() const;
    std::vector<TableEntry> get_tables() const;
    std::unordered_map<uint16_t, NodeState> get_node_health() const;

    // ID generation
    uint64_t next_logical_file_id();
    uint64_t next_file_id();
    uint64_t next_session_id();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, LogicalFileEntry> files_;     // by logical_file_id
    std::unordered_map<uint64_t, uint64_t> sessions_;          // session_id → file_id
    std::vector<TableEntry> tables_;
    std::unordered_map<uint16_t, NodeState> node_health_;
    std::atomic<uint64_t> next_logical_file_id_{1};
    std::atomic<uint64_t> next_file_id_{1};
    std::atomic<uint64_t> next_session_id_{1};
};

}  // namespace filegroup
