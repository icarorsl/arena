#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "common/types.h"

namespace filegroup {

/**
 * Manifest entry header — precedes all manifest entries.
 * All entries have this header, then their specific body.
 */
struct ManifestEntryHeader {
    uint64_t entry_lsn;           // Monotonically increasing per group (8 bytes)
    uint64_t timestamp_us;         // When entry was written (Unix microseconds) (8 bytes)
    uint32_t crc32c;              // CRC32C of bytes after this header (4 bytes)
    uint16_t entry_type;          // ManifestEntryType enum value (2 bytes)
    uint16_t length;              // Body length in bytes (2 bytes)
    // Total: 24 bytes
};

static_assert(sizeof(ManifestEntryHeader) == 24, "ManifestEntryHeader must be 24 bytes");
static constexpr uint8_t MANIFEST_VERSION = 0x01;

// ===== Phase 1 Entry Types =====

/**
 * SESSION_OPEN: Engine opens a new upload session.
 * Contains resolved upload configuration.
 */
struct SessionOpenEntry {
    uint64_t session_id;
    uint64_t file_id;
    uint64_t logical_file_id;
    uint16_t table_id;
    uint32_t group_id;
    uint32_t version_number;
    uint64_t chunk_size;            // Resolved chunk size
    uint8_t  replication_factor;    // Resolved replication
    uint32_t expected_chunks;       // Total chunks expected
    uint64_t expires_at;            // Resolved expiry timestamp
    uint8_t  encryption;            // EncryptionAlgo value
    uint8_t  segment_type;          // SegmentType: STANDARD or PAGE
    // page_bucket: variable-length string (DAY/WEEK/MONTH bucket name)
};

/**
 * CHUNK_CONFIRMED: Storage nodes have persisted a chunk.
 * Contains replica locations.
 */
struct ChunkConfirmedEntry {
    uint64_t session_id;
    uint64_t file_id;
    uint32_t chunk_index;
    uint64_t chunk_size_actual;    // Actual bytes stored
    uint32_t chunk_checksum;       // CRC32C of plaintext chunk
    uint8_t  replica_count;        // Number of replicas
    uint64_t segment_offset;       // Offset in segment file (header offset)
    char     segment_file[256];    // Path to segment file
};

/**
 * VERSION_COMPLETE: All chunks confirmed, file is readable.
 */
struct VersionCompleteEntry {
    uint64_t file_id;
    uint64_t logical_file_id;
    uint32_t version_number;
    uint32_t content_checksum;     // CRC32C of full file
    uint64_t total_size;
    uint32_t chunk_count;
    uint64_t created_at_us;
};

/**
 * VERSION_DELETED: Version marked for deletion (cleanup may be async).
 */
struct VersionDeletedEntry {
    uint64_t file_id;
    uint64_t logical_file_id;
    uint32_t version_number;
};

/**
 * FILE_DELETED: Logical file marked for deletion.
 */
struct FileDeletedEntry {
    uint64_t logical_file_id;
};

/**
 * SESSION_TIMED_OUT: Upload session expired without completion.
 */
struct SessionTimedOutEntry {
    uint64_t session_id;
    uint64_t file_id;
};

/**
 * CHUNK_DELETE_CONFIRMED: Storage node confirmed chunk deletion.
 */
struct ChunkDeleteConfirmedEntry {
    uint64_t file_id;
    uint32_t chunk_index;
    uint16_t node_id;
};

/**
 * PAGE_EXPIRED: A page-segment bucket has expired.
 * All versions in that bucket should be marked EXPIRED.
 */
struct PageExpiredEntry {
    uint32_t group_id;
    uint16_t table_id;
    uint64_t expiry_bucket_us;    // Bucket start time
    uint8_t  granularity;          // ExpiryGranularity
};

/**
 * PAGE_DELETED: Page segment file has been deleted.
 */
struct PageDeletedEntry {
    uint32_t group_id;
    uint16_t table_id;
    uint64_t expiry_bucket_us;
    uint8_t  node_count;
    // node_ids follow: uint16_t * node_count
};

/**
 * NODE_HEALTH: Storage node state change.
 */
struct NodeHealthEntry {
    uint16_t node_id;
    uint8_t  state;               // NodeState enum
};

/**
 * MAX_VERSIONS_ENFORCED: Oldest version deleted to enforce max_versions.
 */
struct MaxVersionsEnforcedEntry {
    uint64_t logical_file_id;
    uint32_t deleted_version_number;
    uint64_t deleted_file_id;
};

/**
 * CHUNK_LOCATION_UPDATED: After compaction, chunk moved to new segment.
 */
struct ChunkLocationUpdatedEntry {
    uint64_t file_id;
    uint32_t chunk_index;
    uint16_t node_id;
    std::string segment_file;    // New segment path
    uint64_t offset;             // New offset in segment
};

/**
 * TABLE_CREATED: A new file table was created dynamically.
 * Tables are replicated via Raft like all other manifest entries.
 */
struct TableCreatedEntry {
    uint32_t table_id;
    uint32_t group_id;
    char     name[64];
    uint64_t chunk_size;
    uint8_t  replication_factor;
    uint8_t  encryption;
    uint32_t max_versions;
    uint32_t file_expires_in_days;
    uint8_t  expiry_granularity;
};

// Phase 3+ entry types (declared for forward compatibility)
// PROJECTION_* entries: ProjectionEntry structures (Phase 3 — not implemented)

}  // namespace filegroup
