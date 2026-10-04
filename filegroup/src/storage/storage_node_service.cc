#include "storage/storage_node_service.h"

#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace filegroup {

// ============================================================================
// StorageServer
// ============================================================================

StorageServer::StorageServer(uint16_t node_id, const std::string& data_dir,
                               uint64_t segment_size_max)
    : node_id_(node_id)
    , data_dir_(data_dir)
    , segment_size_max_(segment_size_max)
{
    // Ensure data directory exists
    mkdir(data_dir_.c_str(), 0755);
}

StorageServer::~StorageServer() = default;

StoreChunkResult StorageServer::store_chunk(
    uint64_t file_id, uint32_t chunk_index,
    uint32_t group_id, uint32_t table_id,
    const uint8_t* data, uint64_t size,
    uint32_t chunk_checksum, bool is_encrypted,
    uint64_t expires_at_us, ExpiryGranularity page_granularity)
{
    std::lock_guard<std::mutex> lock(mutex_);

    StoreChunkResult result;

    // Determine if this is a page segment (has expiry) or standard
    uint64_t bucket_start = 0;
    if (expires_at_us > 0) {
        bucket_start = PageSegment::expiry_bucket_start_us(page_granularity, expires_at_us);
    }

    SegmentKey key{group_id, table_id, bucket_start};

    try {
        // Get or create the active segment
        Segment* seg = get_active_segment(group_id, table_id, expires_at_us);
        if (!seg) {
            result.error = "Failed to create segment";
            return result;
        }

        uint64_t offset = seg->write_chunk(file_id, chunk_index, data, size,
                                            chunk_checksum, is_encrypted);

        result.success = true;
        result.segment_file = seg->path();
        result.offset = offset;

        // Index the chunk for future fetch/delete
        chunk_index_[file_id][chunk_index] = {seg->path(), offset};
    } catch (const std::exception& e) {
        result.error = e.what();
    }

    return result;
}

FetchChunkResult StorageServer::fetch_chunk(const std::string& segment_file,
                                              uint64_t offset, uint64_t length)
{
    FetchChunkResult result;

    try {
        Segment seg(segment_file);
        result.data = seg.read_chunk(offset, length);
        result.success = true;
    } catch (const std::exception& e) {
        result.error = e.what();
    }

    return result;
}

bool StorageServer::delete_chunk(uint64_t file_id, uint32_t chunk_index) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto fit = chunk_index_.find(file_id);
    if (fit == chunk_index_.end()) return false;

    auto cit = fit->second.find(chunk_index);
    if (cit == fit->second.end()) return false;

    try {
        Segment seg(cit->second.segment_file);
        bool ok = seg.mark_deleted(cit->second.offset);
        fit->second.erase(cit);
        return ok;
    } catch (...) {
        return false;
    }
}

bool StorageServer::delete_page(const std::string& page_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    return PageSegment::unlink_page(page_path);
}

void StorageServer::invalidate_segment(uint32_t group_id, uint32_t table_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    SegmentKey key{group_id, table_id, 0};
    segments_.erase(key);
}

bool StorageServer::ping() const {
    return true;
}

std::vector<StorageServer::SegmentInventory> StorageServer::report_segments() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SegmentInventory> inventory;

    for (const auto& [key, seg] : segments_) {
        SegmentInventory inv;
        inv.segment_file = seg->path();
        inv.total_bytes = 0;
        inv.used_bytes = seg->total_data_bytes();
        inventory.push_back(inv);
    }

    return inventory;
}

std::vector<std::string> StorageServer::list_segments() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> paths;
    for (const auto& [key, seg] : segments_) {
        paths.push_back(seg->path());
    }
    return paths;
}

Segment* StorageServer::get_active_segment(uint32_t group_id, uint32_t table_id,
                                             uint64_t expires_at_us) {
    SegmentKey key{group_id, table_id, 0};
    if (expires_at_us > 0) {
        // Page segments are keyed by expiry bucket, but we compute that in store_chunk
        key.expires_bucket = 0; // Simplified: use table-level segments
    }

    auto it = segments_.find(key);
    if (it != segments_.end()) {
        return it->second.get();
    }

    // Create new segment
    std::string path = segment_filename(group_id, table_id, expires_at_us, segment_sequence_++);

    auto seg = std::make_unique<Segment>(path, true);
    seg->set_ownership(node_id_, group_id, (uint16_t)table_id);
    Segment* ptr = seg.get();
    segments_[key] = std::move(seg);
    return ptr;
}

std::string StorageServer::segment_filename(uint32_t group_id, uint32_t table_id,
                                              uint64_t expires_at_us, uint32_t sequence) const {
    std::ostringstream oss;
    if (expires_at_us > 0) {
        oss << data_dir_ << "/page_" << node_id_ << "_" << group_id
            << "_" << table_id << "_" << sequence << ".seg";
    } else {
        oss << data_dir_ << "/seg_" << node_id_ << "_" << group_id
            << "_" << table_id << "_" << sequence << ".seg";
    }
    return oss.str();
}

// ============================================================================
// StorageClient
// ============================================================================

StorageClient::StorageClient(StorageServer* server)
    : server_(server), node_id_(server->node_id()) {}

StoreChunkResult StorageClient::store_chunk(
    uint64_t file_id, uint32_t chunk_index,
    uint32_t group_id, uint32_t table_id,
    const uint8_t* data, uint64_t size,
    uint32_t chunk_checksum, bool is_encrypted,
    uint64_t expires_at_us, ExpiryGranularity page_granularity)
{
    return server_->store_chunk(file_id, chunk_index, group_id, table_id,
                                 data, size, chunk_checksum, is_encrypted,
                                 expires_at_us, page_granularity);
}

FetchChunkResult StorageClient::fetch_chunk(const std::string& segment_file,
                                              uint64_t offset, uint64_t length) {
    return server_->fetch_chunk(segment_file, offset, length);
}

bool StorageClient::delete_chunk(uint64_t file_id, uint32_t chunk_index) {
    return server_->delete_chunk(file_id, chunk_index);
}

bool StorageClient::delete_page(const std::string& page_path) {
    return server_->delete_page(page_path);
}

void StorageClient::invalidate_segment(uint32_t group_id, uint32_t table_id) {
    server_->invalidate_segment(group_id, table_id);
}

bool StorageClient::ping() { return server_->ping(); }

uint16_t StorageClient::node_id() const { return node_id_; }

std::vector<std::string> StorageClient::list_segments() const {
    return server_->list_segments();
}

}  // namespace filegroup
