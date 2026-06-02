#include "upload/upload_protocol.h"
#include "distribution/distribution.h"
#include "encryption/encryption.h"
#include "common/crc32c.h"
#include "common/clock.h"
#include "storage/storage_node_service.h"
#include <stdexcept>
#include <cstring>

namespace filegroup {

UploadProtocol::UploadProtocol() : next_session_id_(1) {
}

UploadSession UploadProtocol::create_session(
    const std::string& filename,
    uint64_t file_size,
    const SessionConfig& config) {
    
    if (filename.empty()) {
        throw std::invalid_argument("filename is empty");
    }
    
    if (file_size == 0) {
        throw std::invalid_argument("file_size must be > 0");
    }
    
    if (config.replication_factor == 0) {
        throw std::invalid_argument("replication_factor must be > 0");
    }
    
    // Calculate number of chunks
    uint32_t total_chunks = (file_size + config.chunk_size_bytes - 1) / config.chunk_size_bytes;
    
    // Create session
    UploadSession session;
    session.session_id = next_session_id_++;
    session.logical_file_id = next_session_id_ * 1000;  // Simple ID generation
    session.version_number = 1;
    session.filename = filename;
    session.file_size_bytes = file_size;
    session.total_chunks = total_chunks;
    session.uploaded_chunks = 0;
    session.status = "open";
    session.created_at_us = now_us();
    
    sessions_[session.session_id] = session;
    
    return session;
}

ChunkUploadResponse UploadProtocol::upload_chunk(const ChunkUploadRequest& request) {
    auto it = sessions_.find(request.session_id);
    if (it == sessions_.end()) {
        return {false, request.chunk_index, {}, "session not found"};
    }
    
    auto& session = it->second;
    
    if (request.chunk_index >= session.total_chunks) {
        return {false, request.chunk_index, {}, "chunk_index out of range"};
    }
    
    // Encrypt chunk if needed
    std::string chunk_data = request.chunk_data;
    if (session.status != "open") {
        return {false, request.chunk_index, {}, "session not in open state"};
    }
    
    // Compute checksum
    uint32_t checksum = crc32c(
        reinterpret_cast<const uint8_t*>(chunk_data.data()),
        chunk_data.size()
    );
    
    // Distribute to nodes (get 3 nodes by default)
    std::vector<uint32_t> available_nodes = {1, 2, 3, 4, 5};  // Simulated available nodes
    uint8_t replication_factor = 3;
    
    auto placements = distribute_chunks(1, replication_factor, available_nodes);
    
    if (placements.empty()) {
        return {false, request.chunk_index, {}, "distribution failed"};
    }
    
    ChunkUploadResponse resp;
    resp.success = true;
    resp.chunk_index = request.chunk_index;
    resp.assigned_nodes = placements[0].node_ids;
    
    // Upload to assigned nodes
    for (uint32_t node_id : resp.assigned_nodes) {
        try {
            auto storage_service = StorageNodeService::get_node_service(node_id);
            
            UploadChunkRequest storage_req;
            storage_req.chunk_index = request.chunk_index;
            storage_req.chunk_data = chunk_data;
            storage_req.chunk_checksum = checksum;
            storage_req.replication_factor = replication_factor;
            
            auto storage_resp = storage_service->upload_chunk(storage_req);
            if (!storage_resp.success) {
                return {false, request.chunk_index, resp.assigned_nodes, 
                        "failed to upload to node " + std::to_string(node_id)};
            }
        } catch (const std::exception& e) {
            return {false, request.chunk_index, resp.assigned_nodes, 
                    std::string("exception: ") + e.what()};
        }
    }
    
    // Update session
    session.uploaded_chunks++;
    
    return resp;
}

bool UploadProtocol::finalize_session(uint32_t session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }
    
    auto& session = it->second;
    
    if (session.uploaded_chunks != session.total_chunks) {
        return false;  // Not all chunks uploaded
    }
    
    session.status = "complete";
    return true;
}

UploadSession UploadProtocol::get_session(uint32_t session_id) const {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        throw std::invalid_argument("session not found");
    }
    return it->second;
}

bool UploadProtocol::cancel_session(uint32_t session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }
    
    it->second.status = "cancelled";
    return true;
}

bool UploadProtocol::has_session(uint32_t session_id) const {
    return sessions_.find(session_id) != sessions_.end();
}

}  // namespace filegroup
