#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "common/types.h"

namespace filegroup {

/**
 * Read Protocol Service (Phase 1)
 *
 * Handles file downloads by:
 * 1. Locating chunks across storage nodes
 * 2. Reading from available replicas with failover
 * 3. Verifying chunk integrity (CRC32C)
 * 4. Optionally decrypting if needed
 * 5. Reassembling file from chunks
 *
 * Supports:
 * - Parallel chunk reads from multiple nodes
 * - Replica failover on read failures
 * - Partial reads (specific byte ranges)
 * - Version selection
 *
 * Phase 2 — full read protocol session management not yet implemented.
 * Byte-range reads are handled directly by Engine::read_range().
 */

struct ReadSessionConfig {
    uint32_t file_id;
    uint32_t version_id;        // 0 = latest complete
    uint64_t offset_bytes;      // Start offset in file (0 = beginning)
    uint64_t length_bytes;      // Bytes to read (0 = until end)
    bool verify_checksum;       // Verify CRC32C per chunk
    bool decrypt;               // Decrypt chunks if encrypted
    std::string passphrase;     // Decryption passphrase (if decrypt=true)
};

struct ReadSessionInfo {
    uint32_t session_id;
    uint32_t file_id;
    uint32_t version_id;
    uint64_t offset_bytes;
    uint64_t length_bytes;
    uint64_t chunks_total;
    uint64_t chunks_read;
    std::string status;         // "open", "reading", "complete", "error", "cancelled"
    std::string error;
    uint64_t bytes_read;        // Total bytes successfully read
};

struct ChunkReadRequest {
    uint32_t session_id;
    uint32_t chunk_index;
    bool verify_checksum;       // Verify checksum after reading
};

struct ChunkReadResponse {
    bool success;
    uint32_t chunk_index;
    std::string chunk_data;
    uint32_t chunk_checksum;
    std::vector<uint32_t> node_ids_tried;  // Which replicas we tried
    uint32_t successful_node_id;           // Which node succeeded
    std::string error;
};

struct NodeLocation {
    uint32_t node_id;
    uint32_t chunk_index;
};

class ReadProtocol {
public:
    ReadProtocol();
    
    /**
     * Open a read session for a file.
     *
     * Returns:
     *   Session info with status "open" on success
     *
     * Throws:
     *   std::invalid_argument if file_id is 0
     *   std::runtime_error if file not found or has no complete versions
     */
    ReadSessionInfo open_session(const ReadSessionConfig& config);
    
    /**
     * Check if session exists.
     */
    bool has_session(uint32_t session_id) const;
    
    /**
     * Get session info.
     */
    ReadSessionInfo get_session(uint32_t session_id) const;
    
    /**
     * Read a single chunk with replica failover.
     *
     * Strategy:
     * 1. Query file index for chunk locations
     * 2. Try each replica in order until one succeeds
     * 3. On first read failure, skip to next replica
     * 4. Optional checksum verification
     * 5. Update session progress
     *
     * Returns:
     *   Response with chunk_data and successful_node_id
     *
     * Note: If all replicas fail, returns success=false with error details
     */
    ChunkReadResponse read_chunk(const ChunkReadRequest& request);
    
    /**
     * Complete a read session and return final status.
     *
     * Returns:
     *   true if session was successfully finalized
     */
    bool complete_session(uint32_t session_id);
    
    /**
     * Cancel a read session.
     *
     * Returns:
     *   true if session was cancelled, false if not found
     */
    bool cancel_session(uint32_t session_id);

private:
    struct ReadSession {
        uint32_t session_id;
        ReadSessionConfig config;
        uint64_t chunks_total;
        uint64_t chunks_read;
        std::string status;
        std::string error;
        uint64_t bytes_read;
        std::map<uint32_t, bool> chunk_read_status;  // chunk_index -> read_success
    };
    
    std::map<uint32_t, ReadSession> sessions_;
    uint32_t next_session_id_ = 1;
    
    /**
     * Get list of node locations for a chunk.
     * Returns empty vector if chunk not found.
     */
    std::vector<uint32_t> get_chunk_nodes(uint32_t file_id, uint32_t version_id, uint32_t chunk_index);
};

}  // namespace filegroup
