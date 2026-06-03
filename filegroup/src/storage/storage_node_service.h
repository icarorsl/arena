#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.h"
#include "segment/segment.h"

namespace filegroup {

// ============================================================================
// StorageServer — In-process storage node (Phase 1)
// Owns segment files. Handles StoreChunk, FetchChunk, DeleteChunk, DeletePage.
// Phase 2+: Will be wrapped with gRPC.
// ============================================================================

/// Result of a StoreChunk operation.
struct StoreChunkResult {
    bool success = false;
    std::string segment_file;   // Path on this node
    uint64_t offset = 0;         // Byte offset within segment
    std::string error;
};

/// Result of a FetchChunk operation.
struct FetchChunkResult {
    bool success = false;
    std::vector<uint8_t> data;
    std::string error;
};

class StorageServer {
public:
    /// Create a storage server.
    /// @param node_id This node's ID
    /// @param data_dir Directory where segment files are stored
    /// @param segment_size_max Max bytes per segment before rolling over
    StorageServer(uint16_t node_id, const std::string& data_dir,
                  uint64_t segment_size_max = 256ULL * 1024 * 1024);
    ~StorageServer();

    // ---- Chunk operations ----

    /// Store a chunk on this node (primary or replica).
    /// If expires_at_us > 0, routes to a page segment. Otherwise standard segment.
    StoreChunkResult store_chunk(
        uint64_t file_id, uint32_t chunk_index,
        uint32_t group_id, uint32_t table_id,
        const uint8_t* data, uint64_t size,
        uint32_t chunk_checksum, bool is_encrypted,
        uint64_t expires_at_us, ExpiryGranularity page_granularity);

    /// Fetch a chunk from this node.
    FetchChunkResult fetch_chunk(const std::string& segment_file,
                                  uint64_t offset, uint64_t length);

    /// Delete a chunk (marks deleted, doesn't reclaim space yet).
    bool delete_chunk(uint64_t file_id, uint32_t chunk_index);

    /// Delete an entire page segment file.
    bool delete_page(const std::string& page_path);

    /// Health check.
    bool ping() const;

    /// Report local segment inventory (for recovery).
    struct SegmentInventory {
        std::string segment_file;
        uint64_t total_bytes = 0;
        uint64_t used_bytes = 0;
        std::vector<std::pair<uint64_t, uint32_t>> chunks; // (file_id, chunk_index)
    };
    std::vector<SegmentInventory> report_segments() const;

    /// List all segment file paths on this node (for compaction).
    std::vector<std::string> list_segments() const;

    /// Get the data directory path.
    const std::string& data_dir() const { return data_dir_; }

    // ---- Node info ----
    uint16_t node_id() const { return node_id_; }

private:
    /// Get or create the active segment for the given group/table/expiry.
    Segment* get_active_segment(uint32_t group_id, uint32_t table_id,
                                 uint64_t expires_at_us);
    std::string segment_filename(uint32_t group_id, uint32_t table_id,
                                  uint64_t expires_at_us, uint32_t sequence) const;

    uint16_t node_id_;
    std::string data_dir_;
    uint64_t segment_size_max_;

    // Active segments: key = (group_id, table_id)
    // For standard segments (no expiry), expires=0 means standard
    // For page segments, we key by bucket name
    struct SegmentKey {
        uint32_t group_id;
        uint32_t table_id;
        uint64_t expires_bucket; // 0 = standard segment
        bool operator==(const SegmentKey& o) const {
            return group_id == o.group_id && table_id == o.table_id
                && expires_bucket == o.expires_bucket;
        }
    };
    struct SegmentKeyHash {
        size_t operator()(const SegmentKey& k) const {
            return k.group_id ^ (k.table_id << 16) ^ (k.expires_bucket >> 32);
        }
    };

    mutable std::mutex mutex_;
    std::unordered_map<SegmentKey, std::unique_ptr<Segment>, SegmentKeyHash> segments_;
    uint32_t segment_sequence_ = 0;

    // Chunk offset index: (file_id, chunk_index) -> (segment_file, offset)
    // Used for fetch/delete without scanning segments
    struct ChunkLocator {
        std::string segment_file;
        uint64_t offset;
    };
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, ChunkLocator>> chunk_index_;
};

// ============================================================================
// StorageClient — Engine-side client (Phase 1: in-process reference)
// ============================================================================

class StorageClient {
public:
    explicit StorageClient(StorageServer* server);

    StoreChunkResult store_chunk(
        uint64_t file_id, uint32_t chunk_index,
        uint32_t group_id, uint32_t table_id,
        const uint8_t* data, uint64_t size,
        uint32_t chunk_checksum, bool is_encrypted,
        uint64_t expires_at_us = 0,
        ExpiryGranularity page_granularity = ExpiryGranularity::UNSET);

    FetchChunkResult fetch_chunk(const std::string& segment_file,
                                  uint64_t offset, uint64_t length);

    bool delete_chunk(uint64_t file_id, uint32_t chunk_index);
    bool delete_page(const std::string& page_path);
    bool ping();
    uint16_t node_id() const;
    std::vector<std::string> list_segments() const;
    StorageServer* server() { return server_; }

private:
    StorageServer* server_;
    uint16_t node_id_;
};

}  // namespace filegroup
