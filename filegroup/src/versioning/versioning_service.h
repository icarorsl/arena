#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "common/types.h"

namespace filegroup {

/**
 * Versioning Enforcement Service (Phase 1)
 *
 * Manages file versions with:
 * 1. Version tracking per file
 * 2. Max version limits enforcement
 * 3. Version state management
 * 4. Automatic old version cleanup
 * 5. Version selection for reads
 */

struct VersionInfo {
    uint32_t version_id;
    std::string state;         // "open", "complete", "expired", "deleted"
    uint64_t created_at_us;
    uint64_t completed_at_us;
    uint64_t expires_at_us;
    uint64_t file_size_bytes;
    uint32_t chunk_count;
};

struct VersionConfig {
    uint32_t max_versions;     // Max versions to keep per file
    uint64_t retention_days;   // Days to keep versions
};

class VersioningService {
public:
    VersioningService();
    
    /**
     * Register a new version for a file.
     *
     * Returns:
     *   version_id for the new version
     *
     * Throws:
     *   std::invalid_argument if file_id is 0
     */
    uint32_t create_version(uint32_t file_id, const VersionConfig& config);
    
    /**
     * Mark version as complete.
     *
     * Returns:
     *   true if version was completed
     */
    bool complete_version(uint32_t file_id, uint32_t version_id);
    
    /**
     * Get version info.
     *
     * Returns:
     *   VersionInfo or throws if not found
     */
    VersionInfo get_version(uint32_t file_id, uint32_t version_id);
    
    /**
     * Get latest complete version for a file.
     *
     * Returns:
     *   version_id or 0 if not found
     */
    uint32_t get_latest_complete(uint32_t file_id);
    
    /**
     * Enforce max versions limit.
     *
     * Deletes oldest non-complete versions when limit exceeded.
     *
     * Returns:
     *   Number of versions deleted
     */
    int enforce_max_versions(uint32_t file_id, uint32_t max_versions);
    
    /**
     * Mark version as expired.
     *
     * Returns:
     *   true if version was marked expired
     */
    bool expire_version(uint32_t file_id, uint32_t version_id);
    
    /**
     * Get all versions for a file.
     */
    std::vector<VersionInfo> list_versions(uint32_t file_id);

private:
    struct FileVersions {
        std::map<uint32_t, VersionInfo> versions;  // version_id -> VersionInfo
        uint32_t next_version_id = 1;
    };
    
    std::map<uint32_t, FileVersions> files_;  // file_id -> FileVersions
};

}  // namespace filegroup
