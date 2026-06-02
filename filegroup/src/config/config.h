#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "common/types.h"

namespace filegroup {

// Encryption key configuration
struct EncryptionKeyConfig {
    std::string file;  // Path to key file (must be exactly 32 bytes)
};

// File table configuration
struct FileTableConfig {
    uint16_t table_id;
    std::string name;
    uint32_t group_id;

    // Per-table overrides (0 = inherit from group, unless noted otherwise)
    uint64_t chunk_size;                 // bytes, 0 = inherit
    uint8_t replication_factor;          // 0 = inherit
    uint32_t max_versions;               // 0 = inherit, or specific max count
    uint32_t file_expires_in_days;       // 0 = no expiry, or specific days
    ExpiryGranularity expiry_granularity; // UNSET = inherit
    EncryptionAlgo encryption;            // NONE = inherit, explicit algo overrides
    std::optional<EncryptionKeyConfig> encryption_key;  // override group key if set
};

// File group configuration
struct FileGroupConfig {
    uint32_t group_id;
    std::string name;

    // Group-level defaults
    uint64_t chunk_size;                 // bytes, e.g. 65536
    uint64_t min_chunk_bytes;            // minimum allowed chunk size
    uint8_t replication_factor;          // e.g. 3
    uint32_t max_versions;               // e.g. 10
    uint32_t file_expires_in_days;       // 0 = no expiry by default
    ExpiryGranularity expiry_granularity; // DAY, WEEK, MONTH
    EncryptionAlgo encryption;            // NONE or AES_256_GCM
    std::optional<EncryptionKeyConfig> encryption_key;  // if encryption != NONE
};

// Storage node configuration
struct StorageNodeConfig {
    uint16_t node_id;
    std::string address;          // "host:port" for gRPC connection
    NodeRole role;                // Always ORIGIN in Phase 1, EDGE for Phase 2
    std::string region;           // Phase 2 — informational in Phase 1
    std::string cert_file;        // mTLS certificate
    std::string key_file;         // mTLS private key
};

// Registry node configuration
struct RegistryNodeConfig {
    uint16_t id;
    std::string address;          // "host:port" for gRPC
    std::string cert_file;        // mTLS certificate
    std::string key_file;         // mTLS private key
};

// TLS configuration
struct TLSConfig {
    std::string ca_cert_file;          // Cluster root CA certificate
    std::string engine_cert_file;      // Engine's certificate
    std::string engine_key_file;       // Engine's private key
};

// API key configuration
struct ApiKeyConfig {
    std::string key;                    // The actual API key
    std::string name;                   // Human-readable name
    std::vector<uint32_t> groups;       // Groups this key has access to
    std::vector<std::string> permissions;  // "read", "write", "admin", etc.
};

// Complete cluster configuration (from TOML)
struct ClusterConfig {
    // TLS and mTLS setup
    TLSConfig tls;

    // Raft registry nodes (3 nodes for Phase 1)
    std::vector<RegistryNodeConfig> registry_nodes;
    uint32_t election_timeout_ms;      // e.g. 250
    uint32_t heartbeat_interval_ms;    // e.g. 50

    // Storage nodes
    std::vector<StorageNodeConfig> storage_nodes;

    // File groups and tables
    std::vector<FileGroupConfig> groups;
    std::vector<FileTableConfig> tables;

    // API key authentication
    std::vector<ApiKeyConfig> api_keys;

    // Session management
    uint32_t session_timeout_sec;       // e.g. 3600
    uint32_t max_concurrent_sessions;   // per engine

    // Expiry scanning
    uint32_t expiry_scan_interval_sec;  // e.g. 300
    float compaction_threshold;         // e.g. 0.3 (30% deleted space triggers compaction)

    // Heartbeat
    uint32_t heartbeat_interval_sec;    // e.g. 5
};

// Load cluster configuration from TOML file
ClusterConfig load_config(const std::string& config_path);

}  // namespace filegroup
