#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "common/types.h"

namespace filegroup {

/**
 * Standard segment file format (Phase 1).
 * Stores chunks sequentially with metadata.
 *
 * Format:
 *   - Segment header (32 bytes)
 *   - Chunk entries (variable per chunk)
 *     - Chunk header (16 bytes)
 *     - Chunk data (variable)
 */

#pragma pack(push, 1)
struct SegmentHeader {
    uint32_t magic;               // 0xDEADBEEF (4 bytes)
    uint64_t created_at_us;       // Creation timestamp (8 bytes)
    uint64_t total_size;          // Total data size (8 bytes)
    uint32_t chunk_count;         // Number of chunks (4 bytes)
    uint8_t  format_version;      // 0x01 (1 byte)
    uint8_t  segment_type;        // SegmentType: STANDARD or PAGE (1 byte)
    uint8_t  padding[10];         // Align to 40 bytes
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ChunkEntry {
    uint64_t chunk_size;          // Size of chunk data (8 bytes)
    uint32_t chunk_index;         // Index within upload session (4 bytes)
    uint32_t chunk_checksum;      // CRC32C of chunk data (4 bytes)
    uint8_t  replication_factor;  // For tracking (1 byte)
    uint8_t  padding[3];          // Align to 20 bytes
};
#pragma pack(pop)

static_assert(sizeof(SegmentHeader) == 36, "SegmentHeader must be 36 bytes");
static_assert(sizeof(ChunkEntry) == 20, "ChunkEntry must be 20 bytes");

const uint32_t SEGMENT_MAGIC = 0xDEADBEEF;
const uint8_t SEGMENT_FORMAT_VERSION = 0x01;

}  // namespace filegroup
