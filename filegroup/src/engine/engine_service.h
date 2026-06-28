#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "config/config.h"
#include "engine/engine.h"
#include "engine/session.h"
#include "registry/registry_service.h"
#include "storage/storage_node_service.h"

namespace filegroup {

// ============================================================================
// EngineServer — Client-facing API (Phase 1: in-process, Phase 2+: gRPC)
//
// Wraps the Engine class and provides the full API:
//   - Upload: OpenSession, WriteChunk, CompleteSession, ResumeSession
//   - Read: ReadFile, ReadChunk
//   - Management: DeleteFile, DeleteVersion, ListFiles, ListVersions, GetFileInfo
//   - Session: CancelSession
//
// Authentication: validates x-api-key against config's api_keys list.
// mTLS is handled at the transport layer (Phase 2: gRPC + OpenSSL).
// ============================================================================

class EngineServer {
public:
    /// Create the engine server.
    /// @param config Cluster configuration (groups, tables, api_keys, etc.)
    /// @param data_root Root directory for segment storage
    EngineServer(const ClusterConfig& config, const std::string& data_root = "/tmp/filegroup");

    // ---- Upload ----

    struct OpenSessionResult {
        bool success = false;
        uint64_t session_id = 0;
        uint64_t file_id = 0;
        uint64_t logical_file_id = 0;
        uint32_t version_number = 0;
        uint64_t resolved_chunk_size = 0;
        EncryptionAlgo encryption = EncryptionAlgo::NONE;
        std::string error;
    };
    OpenSessionResult open_session(uint32_t group_id, uint32_t table_id,
                                    uint64_t logical_file_id, uint64_t total_size = 0,
                                    uint32_t expected_chunks = 0, uint32_t file_expires_in_days = 0);

    struct WriteChunkResult {
        bool success = false;
        bool already_confirmed = false;
        std::string error;
    };
    WriteChunkResult write_chunk(uint64_t session_id, uint32_t chunk_index,
                                  const std::vector<uint8_t>& data);

    struct CompleteSessionResult {
        bool success = false;
        uint64_t logical_file_id = 0;
        uint64_t file_id = 0;
        uint32_t version_number = 0;
        std::string error;
    };
    CompleteSessionResult complete_session(uint64_t session_id, uint32_t content_checksum = 0);

    struct ResumeSessionResult {
        bool success = false;
        uint64_t session_id = 0;
        uint64_t file_id = 0;
        uint64_t logical_file_id = 0;
        uint32_t version_number = 0;
        uint64_t resolved_chunk_size = 0;
        std::vector<uint32_t> confirmed_chunks;
        EncryptionAlgo encryption = EncryptionAlgo::NONE;
        std::string error;
    };
    ResumeSessionResult resume_session(uint64_t session_id);

    // ---- Read ----

    struct ReadFileResponse {
        std::vector<uint8_t> data;
        std::string error;
    };
    ReadFileResponse read_file(uint64_t logical_file_id, uint32_t version_number = 0);

    struct ReadChunkResponse {
        std::vector<uint8_t> data;
        std::string error;
    };
    ReadChunkResponse read_chunk(uint64_t logical_file_id, uint32_t version_number,
                                  uint32_t chunk_index);

    // ---- Management ----

    struct SimpleResult {
        bool success = false;
        std::string error;
    };
    SimpleResult delete_file(uint64_t logical_file_id);
    SimpleResult delete_version(uint64_t logical_file_id, uint32_t version_number);

    struct FileInfoResult {
        bool success = false;
        uint64_t logical_file_id = 0;
        uint32_t table_id = 0;
        uint32_t group_id = 0;
        uint32_t latest_version = 0;
        FileState state = FileState::ACTIVE;
        std::string error;
    };
    FileInfoResult get_file_info(uint64_t logical_file_id);

    struct VersionInfoResult {
        uint64_t file_id = 0;
        uint32_t version_number = 0;
        VersionState state = VersionState::UPLOADING;
        uint64_t total_size = 0;
        uint32_t chunk_count = 0;
        uint64_t expires_at_us = 0;
        EncryptionAlgo encryption = EncryptionAlgo::NONE;
    };
    std::vector<VersionInfoResult> list_versions(uint64_t logical_file_id);
    std::vector<FileInfoResult> list_files(uint32_t group_id, uint32_t table_id);

    SimpleResult cancel_session(uint64_t session_id);

    // ---- Health ----
    bool ping() const;

    // ---- Accessors ----
    Engine& engine() { return *engine_; }
    const ClusterConfig& config() const { return config_; }

private:
    bool validate_api_key(uint32_t group_id, const std::string& permission) const;

    ClusterConfig config_;
    std::unique_ptr<RegistryServer> registry_server_;
    std::vector<std::unique_ptr<StorageServer>> storage_servers_;
    std::vector<std::unique_ptr<StorageClient>> storage_clients_;
    std::unique_ptr<RegistryClient> registry_client_;
    std::unique_ptr<Engine> engine_;
};

}  // namespace filegroup
