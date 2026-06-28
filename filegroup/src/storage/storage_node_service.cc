#include "storage/storage_node_service.h"
#include <stdexcept>

namespace filegroup {

// Static registry of node services
static std::map<uint32_t, std::shared_ptr<StorageNodeService>> g_node_services;

std::shared_ptr<StorageNodeService> StorageNodeService::get_node_service(uint32_t node_id) {
    auto it = g_node_services.find(node_id);
    if (it != g_node_services.end()) {
        return it->second;
    }
    
    // Create new service for this node
    auto service = std::make_shared<StorageNodeService>(node_id, 100ull * 1024 * 1024 * 1024);  // 100 GB default
    g_node_services[node_id] = service;
    return service;
}

StorageNodeService::StorageNodeService(
    uint32_t node_id,
    uint64_t capacity_bytes)
    : node_id_(node_id),
      capacity_bytes_(capacity_bytes),
      used_bytes_(0) {
}

UploadChunkResponse StorageNodeService::upload_chunk(const UploadChunkRequest& request) {
    // Check if chunk already exists
    if (chunks_.find(request.chunk_index) != chunks_.end()) {
        return {false, request.chunk_index, "chunk already exists"};
    }
    
    // Check capacity
    uint64_t chunk_size = request.chunk_data.size();
    if (used_bytes_ + chunk_size > capacity_bytes_) {
        return {false, request.chunk_index, "insufficient capacity"};
    }
    
    // Store chunk
    StoredChunk stored;
    stored.data = request.chunk_data;
    stored.checksum = request.chunk_checksum;
    stored.replication_factor = request.replication_factor;
    
    chunks_[request.chunk_index] = stored;
    used_bytes_ += chunk_size;
    
    return {true, request.chunk_index, ""};
}

DownloadChunkResponse StorageNodeService::download_chunk(const DownloadChunkRequest& request) {
    auto it = chunks_.find(request.chunk_index);
    if (it == chunks_.end()) {
        return {false, request.chunk_index, "", 0, "chunk not found"};
    }
    
    const auto& stored = it->second;
    return {
        true,
        request.chunk_index,
        stored.data,
        stored.checksum,
        ""
    };
}

bool StorageNodeService::delete_chunk(uint32_t chunk_index) {
    auto it = chunks_.find(chunk_index);
    if (it == chunks_.end()) {
        return false;
    }
    
    used_bytes_ -= it->second.data.size();
    chunks_.erase(it);
    return true;
}

StorageNodeInfo StorageNodeService::get_info() const {
    return {
        node_id_,
        capacity_bytes_,
        used_bytes_,
        static_cast<uint32_t>(chunks_.size())
    };
}

uint64_t StorageNodeService::get_used_bytes() const {
    return used_bytes_;
}

bool StorageNodeService::has_chunk(uint32_t chunk_index) const {
    return chunks_.find(chunk_index) != chunks_.end();
}

void StorageNodeService::reset_all_nodes() {
    g_node_services.clear();
}

}  // namespace filegroup
