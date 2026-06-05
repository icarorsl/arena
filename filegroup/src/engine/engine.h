#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/types.h"
#include "config/config.h"
#include "config/config_resolver.h"
#include "distribution/distribution.h"
#include "encryption/encryption.h"
#include "engine/session.h"
#include "manifest/file_index.h"
#include "manifest/manifest.h"
#include "registry/registry_service.h"
#include "storage/storage_node_service.h"

namespace filegroup {
class Engine {
public:
    Engine(const ClusterConfig& config, RegistryClient* registry, const std::vector<StorageClient*>& storage_nodes);
    UploadSession open_session(uint32_t group_id, uint32_t table_id, uint64_t logical_file_id, uint64_t total_size=0, uint32_t expected_chunks=0, uint32_t file_expires_in_days=0);
    bool write_chunk(uint64_t session_id, uint32_t chunk_index, const uint8_t* data, uint64_t size);
    bool complete_session(uint64_t session_id, uint32_t content_checksum=0, std::string* note=nullptr);
    std::vector<uint32_t> resume_session(uint64_t session_id);
    std::vector<uint8_t> read_file(uint64_t logical_file_id, uint32_t version=0);
    std::vector<uint8_t> read_chunk(uint64_t logical_file_id, uint32_t version, uint32_t chunk_index);
    std::vector<uint8_t> read_range(uint64_t logical_file_id, uint32_t version, uint64_t offset_bytes, uint64_t length_bytes);
    bool read_single_chunk(uint64_t logical_file_id, uint32_t version, uint32_t chunk_index,
                           std::vector<uint8_t>& out);
    const ClusterConfig& config() const { return config_; }
    const UploadSession* get_session(uint64_t session_id) const;

    /// Update a chunk's location after compaction moves it to a new segment.
    void update_chunk_location(uint64_t file_id, uint32_t chunk_index,
                               const std::string& segment_file, uint64_t offset, uint64_t size);

    /// Get storage nodes for direct segment access (compaction).
    const std::vector<StorageClient*>& storage_nodes() const { return storage_nodes_; }

    /// Rebuild chunk_locs_ from segment files on disk (after restart).
    void rebuild_chunk_locations();

private:
    const FileGroupConfig* find_group(uint32_t gid) const;
    const FileTableConfig* find_table(uint32_t tid) const;
    StorageClient* get_storage_node(uint16_t nid);
    ClusterConfig config_; RegistryClient* registry_;
    std::vector<StorageClient*> storage_nodes_;
    std::unordered_map<uint16_t,StorageClient*> node_map_;
    mutable std::mutex sessions_mutex_, chunks_mutex_, inflight_mutex_;
    std::unordered_map<uint64_t,UploadSession> sessions_;
    struct ChunkLoc { std::string sf; uint64_t off,sz; };
    std::unordered_map<uint64_t,std::unordered_map<uint32_t,ChunkLoc>> chunk_locs_;
    std::unordered_map<uint16_t,uint32_t> inflight_reads_;
};
}
