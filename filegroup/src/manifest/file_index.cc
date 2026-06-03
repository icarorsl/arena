#include "manifest/file_index.h"

#include <algorithm>
#include <mutex>
#include "common/clock.h"

namespace filegroup {

FileIndex::FileIndex() = default;

void FileIndex::apply_session_open(const SessionOpenEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // Update high-water ID counters (for recovery replay)
    if (e.logical_file_id >= next_logical_file_id_.load())
        next_logical_file_id_.store(e.logical_file_id + 1);
    if (e.file_id >= next_file_id_.load())
        next_file_id_.store(e.file_id + 1);
    if (e.session_id >= next_session_id_.load())
        next_session_id_.store(e.session_id + 1);
    
    // Create or get file entry
    auto it = files_.find(e.logical_file_id);
    if (it == files_.end()) {
        LogicalFileEntry file_entry;
        file_entry.logical_file_id = e.logical_file_id;
        file_entry.table_id = e.table_id;
        file_entry.group_id = e.group_id;
        file_entry.latest_complete_version = 0;
        file_entry.next_version_number = e.version_number + 1;
        files_[e.logical_file_id] = file_entry;
        it = files_.find(e.logical_file_id);
    }
    
    // Create or get version entry
    auto vit = it->second.versions.find(e.version_number);
    if (vit == it->second.versions.end()) {
        VersionEntry ver;
        ver.file_id = e.file_id;
        ver.version_number = e.version_number;
        ver.state = VersionState::UPLOADING;
        ver.expires_at = e.expires_at;
        ver.total_size = 0;
        ver.chunk_count = e.expected_chunks;
        ver.chunk_size = e.chunk_size;
        ver.replication_factor = e.replication_factor;
        ver.encryption = static_cast<EncryptionAlgo>(e.encryption);
        ver.content_checksum = 0;
        ver.upload_session_id = e.session_id;
        ver.segment_type = static_cast<SegmentType>(e.segment_type);
        ver.page_bucket = "";
        it->second.versions[e.version_number] = ver;
    }
    
    // Track session
    sessions_[e.session_id] = e.logical_file_id;
}

void FileIndex::apply_chunk_confirmed(const ChunkConfirmedEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // Find session and corresponding file/version
    auto sit = sessions_.find(e.session_id);
    if (sit == sessions_.end()) return;  // Unknown session, skip
    
    uint64_t logical_file_id = sit->second;
    auto fit = files_.find(logical_file_id);
    if (fit == files_.end()) return;
    
    // Find version with this session
    for (auto& pair : fit->second.versions) {
        VersionEntry& ver = pair.second;
        if (ver.upload_session_id == e.session_id) {
            // Find or create chunk
            auto cit = std::find_if(ver.chunks.begin(), ver.chunks.end(),
                [&e](const ChunkLocation& c) { return c.chunk_index == e.chunk_index; });
            
            if (cit == ver.chunks.end()) {
                ChunkLocation chunk;
                chunk.chunk_index = e.chunk_index;
                chunk.chunk_size_actual = e.chunk_size_actual;
                chunk.chunk_checksum = e.chunk_checksum;
                if (e.segment_file[0]) {
                    ReplicaLocation rep;
                    rep.segment_file = e.segment_file;
                    rep.offset = e.segment_offset;
                    rep.state = ReplicaState::WRITTEN;
                    chunk.replicas.push_back(rep);
                }
                ver.chunks.push_back(chunk);
            } else {
                cit->chunk_size_actual = e.chunk_size_actual;
                cit->chunk_checksum = e.chunk_checksum;
                if (e.segment_file[0] && cit->replicas.empty()) {
                    ReplicaLocation rep;
                    rep.segment_file = e.segment_file;
                    rep.offset = e.segment_offset;
                    rep.state = ReplicaState::WRITTEN;
                    cit->replicas.push_back(rep);
                }
            }
            ver.total_size += e.chunk_size_actual;
            break;
        }
    }
}

void FileIndex::apply_version_complete(const VersionCompleteEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto fit = files_.find(e.logical_file_id);
    if (fit == files_.end()) return;
    
    auto vit = fit->second.versions.find(e.version_number);
    if (vit == fit->second.versions.end()) return;
    
    vit->second.state = VersionState::COMPLETE;
    vit->second.content_checksum = e.content_checksum;
    vit->second.total_size = e.total_size;
    vit->second.chunk_count = e.chunk_count;
    vit->second.created_at_us = now_us();
    
    fit->second.latest_complete_version = e.version_number;
}

void FileIndex::apply_version_deleted(const VersionDeletedEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto fit = files_.find(e.logical_file_id);
    if (fit == files_.end()) return;
    
    auto vit = fit->second.versions.find(e.version_number);
    if (vit == fit->second.versions.end()) return;
    
    vit->second.state = VersionState::MARKED_DELETED;
}

void FileIndex::apply_version_reclaimed(const VersionReclaimedEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto fit = files_.find(e.logical_file_id);
    if (fit == files_.end()) return;
    
    auto vit = fit->second.versions.find(e.version_number);
    if (vit == fit->second.versions.end()) return;
    
    vit->second.state = VersionState::DELETED;
}

void FileIndex::apply_file_deleted(const FileDeletedEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto fit = files_.find(e.logical_file_id);
    if (fit == files_.end()) return;
    
    // Mark all versions as deleted
    for (auto& pair : fit->second.versions) {
        pair.second.state = VersionState::DELETED;
    }
}

void FileIndex::apply_session_timed_out(const SessionTimedOutEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto sit = sessions_.find(e.session_id);
    if (sit == sessions_.end()) return;
    
    sessions_.erase(sit);
}

void FileIndex::apply_chunk_delete_confirmed(const ChunkDeleteConfirmedEntry& e) {
    // In Phase 1, chunks are not deleted — this entry is for Phase 3+
    // Skip silently
}

void FileIndex::apply_page_deleted(const PageDeletedEntry& e) {
    // Phase 3 feature, skip
}

void FileIndex::apply_node_health(const NodeHealthEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    node_health_[e.node_id] = static_cast<NodeState>(e.state);
}

void FileIndex::apply_max_versions_enforced(const MaxVersionsEnforcedEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto fit = files_.find(e.logical_file_id);
    if (fit == files_.end()) return;
    
    auto vit = fit->second.versions.find(e.deleted_version_number);
    if (vit == fit->second.versions.end()) return;
    
    vit->second.state = VersionState::DELETED;
}

const LogicalFileEntry* FileIndex::get_file(uint64_t logical_file_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = files_.find(logical_file_id);
    return it != files_.end() ? &it->second : nullptr;
}

const VersionEntry* FileIndex::get_latest_complete(uint64_t logical_file_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = files_.find(logical_file_id);
    if (it == files_.end()) return nullptr;

    if (it->second.latest_complete_version == 0) return nullptr;
    
    auto vit = it->second.versions.find(it->second.latest_complete_version);
    return vit != it->second.versions.end() ? &vit->second : nullptr;
}

const VersionEntry* FileIndex::get_version(uint64_t logical_file_id, uint32_t version) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = files_.find(logical_file_id);
    if (it == files_.end()) return nullptr;

    auto vit = it->second.versions.find(version);
    return vit != it->second.versions.end() ? &vit->second : nullptr;
}

std::vector<uint32_t> FileIndex::get_confirmed_chunks(uint64_t session_id) const {
    std::vector<uint32_t> chunks;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto sit = sessions_.find(session_id);
    if (sit == sessions_.end()) return chunks;

    uint64_t logical_file_id = sit->second;
    auto fit = files_.find(logical_file_id);
    if (fit == files_.end()) return chunks;

    // Find version with this session and return confirmed chunks
    for (const auto& pair : fit->second.versions) {
        const VersionEntry& ver = pair.second;
        if (ver.upload_session_id == session_id) {
            for (const auto& chunk : ver.chunks) {
                chunks.push_back(chunk.chunk_index);
            }
            break;
        }
    }
    
    return chunks;
}

bool FileIndex::is_chunk_confirmed(uint64_t session_id, uint32_t chunk_index) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto sit = sessions_.find(session_id);
    if (sit == sessions_.end()) return false;

    uint64_t logical_file_id = sit->second;
    auto fit = files_.find(logical_file_id);
    if (fit == files_.end()) return false;

    // Find version with this session
    for (const auto& pair : fit->second.versions) {
        const VersionEntry& ver = pair.second;
        if (ver.upload_session_id == session_id) {
            return std::any_of(ver.chunks.begin(), ver.chunks.end(),
                [chunk_index](const ChunkLocation& c) { return c.chunk_index == chunk_index; });
        }
    }
    
    return false;
}

std::vector<LogicalFileEntry> FileIndex::list_files(uint16_t table_id, uint32_t group_id) const {
    std::vector<LogicalFileEntry> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);

    for (const auto& p : files_) {
        if (p.second.table_id == table_id && p.second.group_id == group_id) {
            result.push_back(p.second);
        }
    }

    return result;
}

std::vector<LogicalFileEntry> FileIndex::all_files() const {
    std::vector<LogicalFileEntry> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& p : files_) {
        result.push_back(p.second);
    }
    return result;
}

std::unordered_map<uint16_t, NodeState> FileIndex::get_node_health() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return node_health_;
}

uint64_t FileIndex::next_logical_file_id() {
    return next_logical_file_id_.fetch_add(1);
}

uint64_t FileIndex::next_file_id() {
    return next_file_id_.fetch_add(1);
}

uint64_t FileIndex::next_session_id() {
    return next_session_id_.fetch_add(1);
}

void FileIndex::apply_table_created(const TableCreatedEntry& e) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    TableEntry t;
    t.table_id = e.table_id;
    t.group_id = e.group_id;
    t.name = e.name; // char[64] to std::string
    t.chunk_size = e.chunk_size;
    t.replication_factor = e.replication_factor;
    t.encryption = static_cast<EncryptionAlgo>(e.encryption);
    t.max_versions = e.max_versions;
    t.file_expires_in_days = e.file_expires_in_days;
    t.expiry_granularity = static_cast<ExpiryGranularity>(e.expiry_granularity);
    // Replace if same (group_id, table_id) already exists, else append
    for (auto& existing : tables_) {
        if (existing.group_id == t.group_id && existing.table_id == t.table_id) {
            existing = std::move(t);
            return;
        }
    }    tables_.push_back(std::move(t));
}

std::vector<TableEntry> FileIndex::get_tables() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tables_;
}

}  // namespace filegroup
