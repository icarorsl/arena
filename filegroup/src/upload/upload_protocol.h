#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <map>

namespace filegroup {

/**
 * Upload protocol for Phase 1.
 *
 * Manages file upload sessions including:
 * - Session creation and management
 * - Chunk upload with distribution
 * - Encryption and checksum verification
 * - Manifest updates
 */

struct SessionConfig {
    uint32_t replication_factor = 3;
    uint64_t chunk_size_bytes = 1024 * 1024;  // 1 MB default
    std::string encryption = "aes256-gcm";
    std::string passphrase;  // For encryption key derivation
};

struct UploadSession {
    uint32_t session_id;
    uint32_t logical_file_id;
    uint32_t version_number;
    std::string filename;
    uint64_t file_size_bytes;
    uint32_t total_chunks;
    uint32_t uploaded_chunks;
    std::string status;  // "open", "uploading", "finalizing", "complete"
    uint64_t created_at_us;
};

struct ChunkUploadRequest {
    uint32_t session_id;
    uint32_t chunk_index;
    std::string chunk_data;
    bool is_final;  // Last chunk in file
};

struct ChunkUploadResponse {
    bool success;
    uint32_t chunk_index;
    std::vector<uint32_t> assigned_nodes;  // Which nodes stored this chunk
    std::string error;
};

/**
 * Upload protocol manager.
 * Coordinates chunk uploads across storage nodes.
 */
class UploadProtocol {
public:
    UploadProtocol();
    
    /**
     * Create new upload session.
     */
    UploadSession create_session(
        const std::string& filename,
        uint64_t file_size,
        const SessionConfig& config
    );
    
    /**
     * Upload single chunk.
     */
    ChunkUploadResponse upload_chunk(const ChunkUploadRequest& request);
    
    /**
     * Finalize upload session.
     */
    bool finalize_session(uint32_t session_id);
    
    /**
     * Get session info.
     */
    UploadSession get_session(uint32_t session_id) const;
    
    /**
     * Cancel session and cleanup.
     */
    bool cancel_session(uint32_t session_id);
    
    /**
     * Check if session exists.
     */
    bool has_session(uint32_t session_id) const;

private:
    std::map<uint32_t, UploadSession> sessions_;
    uint32_t next_session_id_;
};

}  // namespace filegroup
