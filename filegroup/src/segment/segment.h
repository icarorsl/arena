#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

#include "common/types.h"

namespace filegroup {

// ============================================================================
// Segment File Format (shared between STANDARD and PAGE segments)
// ============================================================================

// Standard segment magic: "DBC CHUN" in little-endian ASCII
constexpr uint64_t STANDARD_SEGMENT_MAGIC = 0x4E5548435F434244ULL;  // "DBC_CHUN"
// Page segment magic: "DBC PAGE" in little-endian ASCII
constexpr uint64_t PAGE_SEGMENT_MAGIC = 0x454741505F434244ULL;      // "DBC_PAGE"
constexpr uint8_t  SEGMENT_VERSION = 0x01;
constexpr uint64_t SEGMENT_SIZE_MAX = 256ULL * 1024 * 1024;  // 256 MB default

#pragma pack(push, 1)
struct SegmentFileHeader {
    uint64_t magic;              // STANDARD_SEGMENT_MAGIC or PAGE_SEGMENT_MAGIC (8 bytes)
    uint8_t  format_version;     // 0x01 (1 byte)
    uint8_t  segment_type;       // SegmentType: STANDARD or PAGE (1 byte)
    uint16_t node_id;            // Storage node that owns this segment (2 bytes)
    uint32_t group_id;           // File group (4 bytes)
    uint16_t table_id;           // Table ID (2 bytes, 0 for page segments)
    uint64_t created_at_us;      // Creation timestamp (8 bytes)
    uint64_t write_offset;       // Current write position (8 bytes)
    uint32_t chunk_count;        // Number of chunks written (4 bytes)
    uint64_t total_data_bytes;   // Sum of all chunk sizes (8 bytes)
    uint32_t header_crc32c;      // CRC32C of preceding header bytes (4 bytes)
    uint8_t  reserved[14];       // Reserved, must be zero (14 bytes)
};
#pragma pack(pop)
static_assert(sizeof(SegmentFileHeader) == 64, "SegmentFileHeader must be 64 bytes");

#pragma pack(push, 1)
struct ChunkEntryHeader {
    uint64_t file_id;            // (8 bytes)
    uint32_t chunk_index;        // (4 bytes)
    uint64_t chunk_size;         // Actual bytes of chunk data (8 bytes)
    uint32_t chunk_checksum;     // CRC32C of chunk data (4 bytes)
    uint8_t  is_deleted;         // 0 = active, 1 = deleted (1 byte)
    uint8_t  is_encrypted;       // 0 = plaintext, 1 = encrypted (1 byte)
    uint8_t  reserved[6];        // (6 bytes)
    // Total: 32 bytes
};
#pragma pack(pop)
static_assert(sizeof(ChunkEntryHeader) == 32, "ChunkEntryHeader must be 32 bytes");

// ============================================================================
// Segment — Standard segment file (append-only chunks, compaction support)
// ============================================================================

class Segment {
public:
    /// Open an existing segment file for reading/writing.
    /// @param path Path to the segment file on disk
    /// @param create If true, create a new file (fails if exists)
    explicit Segment(const std::string& path, bool create = false);
    ~Segment();

    // ---- Write operations ----

    /// Write a chunk to the segment. Acquires mutex, pwrite, advances write_offset.
    /// @return byte offset within the file where chunk was written
    uint64_t write_chunk(uint64_t file_id, uint32_t chunk_index,
                         const uint8_t* data, uint64_t size,
                         uint32_t chunk_checksum, bool is_encrypted);

    /// Mark a chunk as deleted at the given offset (for compaction tracking).
    bool mark_deleted(uint64_t offset);

    // ---- Read operations ----

    /// Read raw chunk bytes from offset+length (pread — thread-safe for reads).
    std::vector<uint8_t> read_chunk(uint64_t offset, uint64_t length) const;

    /// Read only the ChunkEntryHeader at the given offset (does not read data).
    ChunkEntryHeader read_chunk_header_at(uint64_t offset) const;

    /// Get the segment header.
    const SegmentFileHeader& header() const { return header_; }

    /// Get the file path.
    const std::string& path() const { return path_; }

    /// Get total number of chunks.
    uint32_t chunk_count() const;

    /// Get total data bytes.
    uint64_t total_data_bytes() const;

private:
    void write_header();
    void read_header();
    uint32_t compute_header_crc32c(const SegmentFileHeader& h) const;

    std::string path_;
    int fd_;                              // File descriptor (low-level I/O)
    SegmentFileHeader header_;
    mutable std::mutex write_mutex_;      // Serializes writes
};

// ============================================================================
// PageSegment — Page-based segment grouped by expiry bucket
// ============================================================================

class PageSegment {
public:
    /// Compute the bucket name string for an expiry time.
    /// DAY: "2026-06-03", WEEK: "2026-W23", MONTH: "2026-06"
    static std::string page_bucket_name(ExpiryGranularity granularity, uint64_t expires_at_us);

    /// Compute the canonical filename for a page segment.
    static std::string page_segment_filename(uint16_t node_id, uint32_t group_id,
                                              uint16_t table_id, const std::string& bucket);

    /// Round an expiry timestamp down to the start of its bucket.
    static uint64_t expiry_bucket_start_us(ExpiryGranularity granularity, uint64_t expires_at_us);

    /// Delete an entire page segment file.
    static bool unlink_page(const std::string& path);

    /// Create/open a page segment (delegates to Segment).
    explicit PageSegment(const std::string& path, bool create = false);

    // Same write/read interface as Segment (delegated)
    uint64_t write_chunk(uint64_t file_id, uint32_t chunk_index,
                         const uint8_t* data, uint64_t size,
                         uint32_t chunk_checksum, bool is_encrypted);
    std::vector<uint8_t> read_chunk(uint64_t offset, uint64_t length) const;
    const SegmentFileHeader& header() const;

private:
    Segment segment_;
};

}  // namespace filegroup
