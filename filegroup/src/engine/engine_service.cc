#include "engine/engine_service.h"

#include <algorithm>

namespace filegroup {

// ============================================================================
// EngineServer
// ============================================================================

EngineServer::EngineServer(const ClusterConfig& config, const std::string& data_root)
    : config_(config)
{
    // Set up storage servers (one per storage node in config)
    for (size_t i = 0; i < config.storage_nodes.size(); i++) {
        auto& node_config = config.storage_nodes[i];
        auto server = std::make_unique<StorageServer>(
            node_config.node_id,
            data_root + "/node_" + std::to_string(node_config.node_id));
        auto client = std::make_unique<StorageClient>(server.get());
        storage_servers_.push_back(std::move(server));
        storage_clients_.push_back(std::move(client));
    }

    // Set up in-process registry (single-node for Phase 1, 3-node for production)
    // For Phase 1 demo: single registry node
    std::vector<uint32_t> registry_peers = {1};
    registry_server_ = std::make_unique<RegistryServer>(1, registry_peers,
        data_root + "/raft.log", 0);

    // Wire up in-process Raft transport (single node, no peers to wire)
    // In a 3-node setup, we'd wire them together here

    // Create registry client
    registry_client_ = std::make_unique<RegistryClient>(
        std::vector<RegistryServer*>{registry_server_.get()});

    // Wait for Raft leader election (single node should become leader quickly)
    registry_server_->wait_for_leader(3'000'000); // 3 second timeout

    // Create the Engine with storage clients
    std::vector<StorageClient*> client_ptrs;
    for (auto& c : storage_clients_) client_ptrs.push_back(c.get());
    engine_ = std::make_unique<Engine>(config, registry_client_.get(), client_ptrs);
}

// ============================================================================
// Upload API
// ============================================================================

EngineServer::OpenSessionResult EngineServer::open_session(
    uint32_t group_id, uint32_t table_id,
    uint64_t logical_file_id, uint64_t total_size,
    uint32_t expected_chunks, uint32_t file_expires_in_days)
{
    OpenSessionResult result;

    try {
        auto session = engine_->open_session(group_id, table_id,
            logical_file_id, total_size, expected_chunks, file_expires_in_days);

        result.success = true;
        result.session_id = session.session_id;
        result.file_id = session.file_id;
        result.logical_file_id = session.logical_file_id;
        result.version_number = session.version_number;
        result.resolved_chunk_size = session.resolved_chunk_size;
        result.encryption = session.resolved_encryption;
    } catch (const std::exception& e) {
        result.error = e.what();
    }

    return result;
}

EngineServer::WriteChunkResult EngineServer::write_chunk(
    uint64_t session_id, uint32_t chunk_index, const std::vector<uint8_t>& data)
{
    WriteChunkResult result;

    // Check if already confirmed (idempotency)
    auto* session = engine_->get_session(session_id);
    if (session && session->confirmed_chunks.count(chunk_index)) {
        result.success = true;
        result.already_confirmed = true;
        return result;
    }

    bool ok = engine_->write_chunk(session_id, chunk_index,
                                    data.data(), data.size());
    result.success = ok;
    if (!ok) result.error = "Write chunk failed";
    return result;
}

EngineServer::CompleteSessionResult EngineServer::complete_session(
    uint64_t session_id, uint32_t content_checksum)
{
    CompleteSessionResult result;

    bool ok = engine_->complete_session(session_id, content_checksum);
    result.success = ok;

    if (ok) {
        auto* session = engine_->get_session(session_id);
        if (session) {
            result.logical_file_id = session->logical_file_id;
            result.file_id = session->file_id;
            result.version_number = session->version_number;
        }
    } else {
        result.error = "Complete session failed";
    }

    return result;
}

EngineServer::ResumeSessionResult EngineServer::resume_session(uint64_t session_id)
{
    ResumeSessionResult result;
    result.session_id = session_id;

    auto* session = engine_->get_session(session_id);
    if (!session) {
        result.error = "Session not found";
        return result;
    }

    result.success = true;
    result.file_id = session->file_id;
    result.logical_file_id = session->logical_file_id;
    result.version_number = session->version_number;
    result.resolved_chunk_size = session->resolved_chunk_size;
    result.encryption = session->resolved_encryption;

    auto chunks = engine_->resume_session(session_id);
    result.confirmed_chunks = std::move(chunks);

    return result;
}

// ============================================================================
// Read API
// ============================================================================

EngineServer::ReadFileResponse EngineServer::read_file(
    uint64_t logical_file_id, uint32_t version_number)
{
    ReadFileResponse result;
    result.data = engine_->read_file(logical_file_id, version_number);
    if (result.data.empty()) {
        result.error = "File not found or read failed";
    }
    return result;
}

EngineServer::ReadChunkResponse EngineServer::read_chunk(
    uint64_t logical_file_id, uint32_t version_number, uint32_t chunk_index)
{
    ReadChunkResponse result;
    result.data = engine_->read_chunk(logical_file_id, version_number, chunk_index);
    if (result.data.empty()) {
        result.error = "Chunk not found or read failed";
    }
    return result;
}

// ============================================================================
// Management API
// ============================================================================

EngineServer::SimpleResult EngineServer::delete_file(uint64_t logical_file_id)
{
    // Write FILE_DELETED to manifest via registry
    FileDeletedEntry entry;
    entry.logical_file_id = logical_file_id;
    auto [success, lsn] = registry_client_->append_entry(
        static_cast<uint32_t>(ManifestEntryType::FILE_DELETED), &entry, sizeof(entry));

    (void)lsn;
    return {success, success ? "" : "Delete file failed"};
}

EngineServer::SimpleResult EngineServer::delete_version(
    uint64_t logical_file_id, uint32_t version_number)
{
    VersionDeletedEntry entry;
    entry.file_id = 0; // Registry resolves this
    entry.logical_file_id = logical_file_id;
    entry.version_number = version_number;
    auto [success, lsn] = registry_client_->append_entry(
        static_cast<uint32_t>(ManifestEntryType::VERSION_DELETED), &entry, sizeof(entry));

    (void)lsn;
    return {success, success ? "" : "Delete version failed"};
}

EngineServer::FileInfoResult EngineServer::get_file_info(uint64_t logical_file_id)
{
    FileInfoResult result;
    result.logical_file_id = logical_file_id;

    auto* file = registry_client_->get_file(logical_file_id);
    if (!file) {
        result.error = "File not found";
        return result;
    }

    result.success = true;
    result.table_id = file->table_id;
    result.group_id = file->group_id;
    result.latest_version = file->latest_complete_version;

    return result;
}

std::vector<EngineServer::VersionInfoResult> EngineServer::list_versions(
    uint64_t logical_file_id)
{
    std::vector<VersionInfoResult> result;

    auto* file = registry_client_->get_file(logical_file_id);
    if (!file) return result;

    for (auto& [vn, ver] : file->versions) {
        VersionInfoResult v;
        v.file_id = ver.file_id;
        v.version_number = ver.version_number;
        v.state = ver.state;
        v.total_size = ver.total_size;
        v.chunk_count = ver.chunk_count;
        v.expires_at_us = ver.expires_at;
        v.encryption = ver.encryption;
        result.push_back(v);
    }

    return result;
}

std::vector<EngineServer::FileInfoResult> EngineServer::list_files(
    uint32_t group_id, uint32_t table_id)
{
    std::vector<FileInfoResult> result;

    auto files = registry_client_->list_files(static_cast<uint16_t>(table_id), group_id);
    for (auto& f : files) {
        FileInfoResult info;
        info.success = true;
        info.logical_file_id = f.logical_file_id;
        info.table_id = f.table_id;
        info.group_id = f.group_id;
        info.latest_version = f.latest_complete_version;
        result.push_back(info);
    }

    return result;
}

EngineServer::SimpleResult EngineServer::cancel_session(uint64_t session_id)
{
    auto* session = engine_->get_session(session_id);
    if (!session) return {false, "Session not found"};

    // Write SESSION_TIMED_OUT to manifest
    SessionTimedOutEntry entry;
    entry.session_id = session_id;
    entry.file_id = session->file_id;
    registry_client_->append_entry(
        static_cast<uint32_t>(ManifestEntryType::SESSION_TIMED_OUT), &entry, sizeof(entry));

    return {true, ""};
}

// ============================================================================
// Health
// ============================================================================

bool EngineServer::ping() const {
    return true;
}

// ============================================================================
// Auth
// ============================================================================

bool EngineServer::validate_api_key(uint32_t group_id, const std::string& permission) const {
    // Phase 1: Simple validation (gRPC interceptor will handle this in Phase 2)
    // Check if any API key in config has access to this group
    for (auto& key : config_.api_keys) {
        for (auto& g : key.groups) {
            if (g == group_id) {
                for (auto& p : key.permissions) {
                    if (p == permission || p == "admin") return true;
                }
            }
        }
    }
    return false;
}

}  // namespace filegroup
