#include "registry/registry_service.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace filegroup {

// ============================================================================
// RegistryServer
// ============================================================================

RegistryServer::RegistryServer(uint32_t node_id,
                               const std::vector<uint32_t>& peer_ids,
                               const std::string& raft_log_path,
                               uint32_t group_id)
    : node_id_(node_id), group_id_(group_id)
{
    RaftConfig raft_config;
    raft_config.local_node_id = node_id;
    raft_config.peer_node_ids = peer_ids;
    raft_config.log_path = raft_log_path;
    raft_config.group_id = group_id;

    // In-process transport (will be registered after all nodes are created)
    transport_ = std::make_unique<InProcessRaftTransport>();

    // Apply callback: when Raft commits an entry, apply it to the file index
    auto apply_fn = [this](uint32_t entry_type, const void* body, uint16_t body_length, uint64_t lsn) {
        (void)lsn; // LSN tracked internally

        auto type = static_cast<ManifestEntryType>(entry_type);
        switch (type) {
            case ManifestEntryType::SESSION_OPEN: {
                if (body_length >= sizeof(SessionOpenEntry)) {
                    file_index_.apply_session_open(*static_cast<const SessionOpenEntry*>(body));
                }
                break;
            }
            case ManifestEntryType::CHUNK_CONFIRMED: {
                if (body_length >= 2) {
                    file_index_.apply_chunk_confirmed(*static_cast<const ChunkConfirmedEntry*>(body));
                }
                break;
            }
            case ManifestEntryType::VERSION_COMPLETE: {
                if (body_length >= sizeof(VersionCompleteEntry)) {
                    file_index_.apply_version_complete(*static_cast<const VersionCompleteEntry*>(body));
                }
                break;
            }
            case ManifestEntryType::VERSION_DELETED: {
                if (body_length >= sizeof(VersionDeletedEntry)) {
                    file_index_.apply_version_deleted(*static_cast<const VersionDeletedEntry*>(body));
                }
                break;
            }
            case ManifestEntryType::FILE_DELETED: {
                if (body_length >= sizeof(FileDeletedEntry)) {
                    file_index_.apply_file_deleted(*static_cast<const FileDeletedEntry*>(body));
                }
                break;
            }
            case ManifestEntryType::SESSION_TIMED_OUT: {
                if (body_length >= sizeof(SessionTimedOutEntry)) {
                    file_index_.apply_session_timed_out(*static_cast<const SessionTimedOutEntry*>(body));
                }
                break;
            }
            case ManifestEntryType::CHUNK_DELETE_CONFIRMED: {
                if (body_length >= sizeof(ChunkDeleteConfirmedEntry)) {
                    file_index_.apply_chunk_delete_confirmed(*static_cast<const ChunkDeleteConfirmedEntry*>(body));
                }
                break;
            }
            case ManifestEntryType::PAGE_DELETED: {
                if (body_length >= sizeof(PageDeletedEntry)) {
                    file_index_.apply_page_deleted(*static_cast<const PageDeletedEntry*>(body));
                }
                break;
            }
            case ManifestEntryType::NODE_HEALTH: {
                if (body_length >= sizeof(NodeHealthEntry)) {
                    file_index_.apply_node_health(*static_cast<const NodeHealthEntry*>(body));
                }
                break;
            }
            case ManifestEntryType::MAX_VERSIONS_ENFORCED: {
                if (body_length >= sizeof(MaxVersionsEnforcedEntry)) {
                    file_index_.apply_max_versions_enforced(*static_cast<const MaxVersionsEnforcedEntry*>(body));
                }
                break;
            }
            default:
                // Phase 3 / unknown entry types — silently skip (forward compatibility)
                std::cerr << "[registry] Unknown manifest entry type: " << entry_type << std::endl;
                break;
        }
    };

    raft_ = std::make_unique<RaftNode>(std::move(raft_config),
                                       std::move(transport_),
                                       std::move(apply_fn));

    std::cout << "[registry] Server created: node=" << node_id_
              << " group=" << group_id_ << std::endl;
}

RegistryServer::~RegistryServer() {
    stop();
}

void RegistryServer::start() {
    // Raft node starts in its constructor (event thread already running)
}

void RegistryServer::stop() {
    raft_.reset();
}

FileIndex& RegistryServer::file_index() { return file_index_; }
const FileIndex& RegistryServer::file_index() const { return file_index_; }
RaftNode& RegistryServer::raft_node() { return *raft_; }
uint32_t RegistryServer::node_id() const { return node_id_; }
bool RegistryServer::is_leader() const { return raft_->is_leader(); }
uint32_t RegistryServer::leader_id() const { return raft_->leader_id(); }

bool RegistryServer::wait_for_leader(uint64_t timeout_us) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(timeout_us);
    while (std::chrono::steady_clock::now() < deadline) {
        if (raft_->is_leader()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return raft_->is_leader();
}

// ============================================================================
// RegistryClient
// ============================================================================

RegistryClient::RegistryClient(std::vector<RegistryServer*> servers)
    : servers_(std::move(servers))
{
}

RegistryServer* RegistryClient::find_leader() {
    for (auto* server : servers_) {
        if (server->is_leader()) {
            return server;
        }
    }
    return nullptr;
}

uint64_t RegistryClient::next_logical_file_id() {
    auto* leader = find_leader();
    return leader ? leader->file_index().next_logical_file_id() : 1000;
}

uint64_t RegistryClient::next_file_id() {
    auto* leader = find_leader();
    return leader ? leader->file_index().next_file_id() : 1000;
}

uint64_t RegistryClient::next_session_id() {
    auto* leader = find_leader();
    return leader ? leader->file_index().next_session_id() : 1;
}

std::pair<bool, uint64_t> RegistryClient::append_entry(
    uint32_t entry_type, const void* body, uint16_t body_length)
{
    // Try each server until we find the leader
    for (auto* server : servers_) {
        if (server->is_leader()) {
            return server->raft_node().propose(entry_type, body, body_length);
        }
    }
    // No leader found — try all servers anyway (one might become leader)
    for (auto* server : servers_) {
        auto [success, lsn] = server->raft_node().propose(entry_type, body, body_length);
        if (success) return {true, lsn};
    }
    return {false, 0};
}

const LogicalFileEntry* RegistryClient::get_file(uint64_t logical_file_id) {
    if (auto* leader = find_leader()) {
        return leader->file_index().get_file(logical_file_id);
    }
    return nullptr;
}

const VersionEntry* RegistryClient::get_latest_complete(uint64_t logical_file_id) {
    if (auto* leader = find_leader()) {
        return leader->file_index().get_latest_complete(logical_file_id);
    }
    return nullptr;
}

const VersionEntry* RegistryClient::get_version(uint64_t logical_file_id, uint32_t version) {
    if (auto* leader = find_leader()) {
        return leader->file_index().get_version(logical_file_id, version);
    }
    return nullptr;
}

std::vector<LogicalFileEntry> RegistryClient::list_files(uint16_t table_id, uint32_t group_id) {
    if (auto* leader = find_leader()) {
        return leader->file_index().list_files(table_id, group_id);
    }
    return {};
}

std::vector<uint32_t> RegistryClient::get_confirmed_chunks(uint64_t session_id) {
    if (auto* leader = find_leader()) {
        return leader->file_index().get_confirmed_chunks(session_id);
    }
    return {};
}

void RegistryClient::report_node_health(uint16_t node_id, NodeState state) {
    NodeHealthEntry entry;
    entry.node_id = node_id;
    entry.state = static_cast<uint8_t>(state);
    append_entry(static_cast<uint32_t>(ManifestEntryType::NODE_HEALTH), &entry, sizeof(entry));
}

std::unordered_map<uint16_t, NodeState> RegistryClient::get_cluster_health() {
    // Query from any server (all have replicated state via Raft)
    for (auto* server : servers_) {
        return server->file_index().get_node_health();
    }
    return {};
}

}  // namespace filegroup
