#pragma once

#include <string>
#include <cstdint>
#include <fstream>
#include <vector>
#include <mutex>

#include "segment/segment.h"

namespace filegroup {

/**
 * Write segments (standard or page format).
 * Appends chunks to segment file with metadata.
 * Thread-safe.
 */
class SegmentWriter {
public:
    /**
     * Create new segment file.
     * @param path path to segment file
     * @param segment_type STANDARD or PAGE
     */
    explicit SegmentWriter(const std::string& path, SegmentType segment_type);

    ~SegmentWriter();

    /**
     * Append chunk to segment.
     * @param chunk_index chunk index within session
     * @param data chunk data pointer
     * @param size chunk size in bytes
     * @param replication_factor replication count
     * @return byte offset within segment file
     */
    uint64_t append_chunk(uint32_t chunk_index, const void* data, uint64_t size, uint8_t replication_factor);

    /**
     * Finalize segment (write header with chunk count).
     * Must be called before closing.
     */
    void finalize();

    /**
     * Get number of chunks appended.
     */
    uint32_t chunk_count() const;

    /**
     * Get total size of chunk data written.
     */
    uint64_t total_size() const;

private:
    std::string path_;
    SegmentType segment_type_;
    std::ofstream file_;
    std::vector<ChunkEntry> chunks_;
    uint64_t total_size_;
    bool finalized_;
    mutable std::mutex mutex_;
};

}  // namespace filegroup
