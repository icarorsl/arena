#pragma once

#include <string>
#include <cstdint>
#include <mutex>
#include <fstream>

#include "manifest/manifest.h"
#include "common/types.h"

namespace filegroup {

/**
 * Append-only manifest log writer.
 * Writes entries to a manifest file with CRC32C checksum per entry.
 * Each write is fsync'd (durability guarantee).
 * Thread-safe.
 */
class ManifestWriter {
public:
    /**
     * Create or open a manifest file.
     * @param path path to manifest file
     * @param group_id file group ID (for LSN tracking)
     */
    explicit ManifestWriter(const std::string& path, uint32_t group_id);

    ~ManifestWriter();

    /**
     * Append an entry to the manifest log.
     * Automatically:
     * - Writes header + body
     * - Computes CRC32C of body
     * - Calls fdatasync() for durability
     * - Increments LSN
     *
     * @param entry_type ManifestEntryType
     * @param body pointer to entry body (type-specific struct)
     * @param body_length length of body in bytes
     * @return LSN of this entry
     */
    uint64_t append(ManifestEntryType entry_type, const void* body, uint16_t body_length);

    /**
     * Get the current LSN (next LSN to assign).
     */
    uint64_t current_lsn() const;

private:
    std::string path_;
    uint32_t group_id_;
    uint64_t next_lsn_;
    std::ofstream file_;
    mutable std::mutex mutex_;
};

}  // namespace filegroup
