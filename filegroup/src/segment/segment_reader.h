#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>

#include "segment/segment.h"

namespace filegroup {

/**
 * Chunk data read from segment.
 */
struct SegmentChunk {
    uint32_t chunk_index;
    std::vector<uint8_t> data;
    uint32_t checksum;
    uint8_t replication_factor;
};

/**
 * Read segments (standard or page format).
 * Validates header and checksums, provides random access to chunks.
 */
class SegmentReader {
public:
    /**
     * Open and read segment file.
     * @param path path to segment file
     */
    explicit SegmentReader(const std::string& path);

    /**
     * Get segment header.
     */
    const SegmentHeader& header() const { return header_; }

    /**
     * Get all chunks.
     */
    const std::vector<SegmentChunk>& chunks() const { return chunks_; }

    /**
     * Get specific chunk by index.
     * @return nullptr if not found
     */
    const SegmentChunk* get_chunk(uint32_t chunk_index) const;

    /**
     * Verify all chunk checksums.
     * @return true if all valid, false if any mismatch
     */
    bool verify_checksums() const;

private:
    SegmentHeader header_;
    std::vector<SegmentChunk> chunks_;
};

}  // namespace filegroup
