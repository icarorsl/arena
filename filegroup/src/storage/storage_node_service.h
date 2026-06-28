#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <map>
#include <memory>

namespace filegroup {

/**
 * Storage Node RPC Service (Phase 1 Mock Implementation)
 *
 * Handles chunk uploads and downloads on storage nodes.
 * In-process implementation for Phase 1.
 * Will be replaced with actual gRPC service in future.
 */

struct UploadChunkRequest {
    uint32_t chunk_index;
    std::string chunk_data;
    uint32_t chunk_checksum;
    uint8_t replication_factor;
};

struct UploadChunkResponse {
    bool success;
    uint32_t chunk_index;
    std::string error;
};

struct DownloadChunkRequest {
    uint32_t chunk_index;
};

struct DownloadChunkResponse {
    bool success;
    uint32_t chunk_index;
    std::string chunk_data;
    uint32_t chunk_checksum;
    std::string error;
};

struct StorageNodeInfo {
    uint32_t node_id;
    uint64_t total_capacity_bytes;
    uint64_t used_bytes;
    uint32_t chunk_count;
};

class StorageNodeService {
public:
    /**
     * Get service instance for a specific node.
     * Each node has its own service instance.
     */
    static std::shared_ptr<StorageNodeService> get_node_service(uint32_t node_id);
    
    /**
     * Initialize service for a node.
     */
    explicit StorageNodeService(
        uint32_t node_id,
        uint64_t capacity_bytes
    );
    
    /**
     * Upload chunk to this node.
     *
     * Parameters:
     *   request: Upload request with chunk data and metadata
     *
     * Returns:
     *   Response with success flag and optional error
     */
    UploadChunkResponse upload_chunk(const UploadChunkRequest& request);
    
    /**
     * Download chunk from this node.
     *
     * Parameters:
     *   request: Download request with chunk index
     *
     * Returns:
     *   Response with chunk data or error
     */
    DownloadChunkResponse download_chunk(const DownloadChunkRequest& request);
    
    /**
     * Delete chunk from this node.
     */
    bool delete_chunk(uint32_t chunk_index);
    
    /**
     * Get storage node info (capacity, used space, chunk count).
     */
    StorageNodeInfo get_info() const;
    
    /**
     * Get used storage space.
     */
    uint64_t get_used_bytes() const;
    
    /**
     * Check if chunk exists.
     */
    bool has_chunk(uint32_t chunk_index) const;
    
    /**
     * Reset all storage nodes (for testing).
     */
    static void reset_all_nodes();

private:
    uint32_t node_id_;
    uint64_t capacity_bytes_;
    uint64_t used_bytes_;
    
    struct StoredChunk {
        std::string data;
        uint32_t checksum;
        uint8_t replication_factor;
    };
    
    std::map<uint32_t, StoredChunk> chunks_;
};

}  // namespace filegroup
