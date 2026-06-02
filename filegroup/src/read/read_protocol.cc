#include "read/read_protocol.h"
#include "storage/storage_node_service.h"
#include "common/crc32c.h"
#include <stdexcept>

namespace filegroup {

ReadProtocol::ReadProtocol() : next_session_id_(1) {}

ReadSessionInfo ReadProtocol::open_session(const ReadSessionConfig& config) {
    if (config.file_id == 0) {
        throw std::invalid_argument("file_id must not be zero");
    }
    
    uint32_t session_id = next_session_id_++;
    
    // In Phase 1, we don't have file index integration yet
    // For now, assume 1 chunk per session
    uint64_t chunks_total = 1;
    
    ReadSession session;
    session.session_id = session_id;
    session.config = config;
    session.chunks_total = chunks_total;
    session.chunks_read = 0;
    session.status = "open";
    session.error = "";
    session.bytes_read = 0;
    
    sessions_[session_id] = session;
    
    ReadSessionInfo info;
    info.session_id = session_id;
    info.file_id = config.file_id;
    info.version_id = config.version_id;
    info.offset_bytes = config.offset_bytes;
    info.length_bytes = config.length_bytes;
    info.chunks_total = chunks_total;
    info.chunks_read = 0;
    info.status = "open";
    info.error = "";
    info.bytes_read = 0;
    
    return info;
}

bool ReadProtocol::has_session(uint32_t session_id) const {
    return sessions_.find(session_id) != sessions_.end();
}

ReadSessionInfo ReadProtocol::get_session(uint32_t session_id) const {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        throw std::runtime_error("session not found: " + std::to_string(session_id));
    }
    
    const auto& session = it->second;
    ReadSessionInfo info;
    info.session_id = session.session_id;
    info.file_id = session.config.file_id;
    info.version_id = session.config.version_id;
    info.offset_bytes = session.config.offset_bytes;
    info.length_bytes = session.config.length_bytes;
    info.chunks_total = session.chunks_total;
    info.chunks_read = session.chunks_read;
    info.status = session.status;
    info.error = session.error;
    info.bytes_read = session.bytes_read;
    
    return info;
}

std::vector<uint32_t> ReadProtocol::get_chunk_nodes(
    uint32_t file_id, uint32_t version_id, uint32_t chunk_index) {
    // In Phase 1, return simulated node locations
    // In production, query file index for actual chunk locations
    return {1, 2, 3};  // Default: 3 replicas
}

ChunkReadResponse ReadProtocol::read_chunk(const ChunkReadRequest& request) {
    auto it = sessions_.find(request.session_id);
    if (it == sessions_.end()) {
        return {false, request.chunk_index, "", 0, {}, 0, "session not found"};
    }
    
    auto& session = it->second;
    
    if (session.status != "open") {
        return {false, request.chunk_index, "", 0, {}, 0, "session not in open state"};
    }
    
    // Get chunk locations
    auto node_ids = get_chunk_nodes(
        session.config.file_id,
        session.config.version_id,
        request.chunk_index
    );
    
    if (node_ids.empty()) {
        session.error = "chunk not found";
        session.status = "error";
        return {false, request.chunk_index, "", 0, {}, 0, "no replicas found"};
    }
    
    // Try each replica
    ChunkReadResponse response;
    response.chunk_index = request.chunk_index;
    
    for (uint32_t node_id : node_ids) {
        response.node_ids_tried.push_back(node_id);
        
        try {
            auto storage_service = StorageNodeService::get_node_service(node_id);
            
            DownloadChunkRequest download_req;
            download_req.chunk_index = request.chunk_index;
            
            auto download_resp = storage_service->download_chunk(download_req);
            
            if (!download_resp.success) {
                continue;  // Try next replica
            }
            
            // Verify checksum if requested
            if (request.verify_checksum) {
                uint32_t computed_checksum = crc32c(
                    reinterpret_cast<const uint8_t*>(download_resp.chunk_data.data()),
                    download_resp.chunk_data.size()
                );
                
                if (computed_checksum != download_resp.chunk_checksum) {
                    continue;  // Try next replica due to checksum mismatch
                }
            }
            
            // Success - update session and return
            response.success = true;
            response.chunk_data = download_resp.chunk_data;
            response.chunk_checksum = download_resp.chunk_checksum;
            response.successful_node_id = node_id;
            response.error = "";
            
            session.chunks_read++;
            session.bytes_read += download_resp.chunk_data.size();
            session.chunk_read_status[request.chunk_index] = true;
            
            return response;
        } catch (const std::exception& e) {
            continue;  // Try next replica
        }
    }
    
    // All replicas failed
    response.success = false;
    response.error = "all replicas failed";
    session.error = "chunk read failed: " + std::string(response.error);
    session.status = "error";
    
    return response;
}

bool ReadProtocol::complete_session(uint32_t session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }
    
    auto& session = it->second;
    
    // Check if all chunks have been read
    if (session.chunks_read >= session.chunks_total) {
        session.status = "complete";
        return true;
    }
    
    return false;
}

bool ReadProtocol::cancel_session(uint32_t session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }
    
    auto& session = it->second;
    session.status = "cancelled";
    
    return true;
}

}  // namespace filegroup
