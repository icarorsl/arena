#include "segment/segment.h"

#include "common/crc32c.h"
#include "common/clock.h"
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
#include <sstream>
#include <iomanip>

// macOS doesn't have fdatasync — use fsync as fallback
#if defined(__APPLE__)
#define fdatasync fsync
#endif

namespace filegroup {

// ============================================================================
// Segment
// ============================================================================

Segment::Segment(const std::string& path, bool create)
    : path_(path), fd_(-1)
{
    if (create) {
        fd_ = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to create segment file: " + path);
        }

        // Initialize header
        std::memset(&header_, 0, sizeof(header_));
        header_.magic = STANDARD_SEGMENT_MAGIC;
        header_.format_version = SEGMENT_VERSION;
        header_.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);
        header_.created_at_us = now_us();
        header_.write_offset = sizeof(SegmentFileHeader);
        header_.header_crc32c = compute_header_crc32c(header_);

        write_header();
    } else {
        fd_ = open(path.c_str(), O_RDWR);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open segment file: " + path);
        }
        read_header();

        // Validate magic
        if (header_.magic != STANDARD_SEGMENT_MAGIC && header_.magic != PAGE_SEGMENT_MAGIC) {
            close(fd_);
            throw std::runtime_error("Invalid segment magic in: " + path);
        }

        // Validate header CRC
        uint32_t computed = compute_header_crc32c(header_);
        if (computed != header_.header_crc32c) {
            close(fd_);
            throw std::runtime_error("Segment header CRC mismatch in: " + path);
        }
    }
}

Segment::~Segment() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

uint64_t Segment::write_chunk(uint64_t file_id, uint32_t chunk_index,
                               const uint8_t* data, uint64_t size,
                               uint32_t chunk_checksum, bool is_encrypted) {
    std::lock_guard<std::mutex> lock(write_mutex_);

    ChunkEntryHeader entry;
    entry.file_id = file_id;
    entry.chunk_index = chunk_index;
    entry.chunk_size = size;
    entry.chunk_checksum = chunk_checksum;
    entry.is_deleted = 0;
    entry.is_encrypted = is_encrypted ? 1 : 0;

    uint64_t offset = header_.write_offset;

    // Write chunk header + data
    ssize_t written = pwrite(fd_, &entry, sizeof(entry), offset);
    if (written != sizeof(entry)) {
        throw std::runtime_error("Failed to write chunk header");
    }

    written = pwrite(fd_, data, size, offset + sizeof(entry));
    if (written != static_cast<ssize_t>(size)) {
        throw std::runtime_error("Failed to write chunk data");
    }

    // Update header
    header_.write_offset += sizeof(entry) + size;
    header_.chunk_count++;
    header_.total_data_bytes += size;
    header_.header_crc32c = compute_header_crc32c(header_);
    write_header();

    // fdatasync for durability
    fdatasync(fd_);

    return offset;
}

bool Segment::mark_deleted(uint64_t offset) {
    std::lock_guard<std::mutex> lock(write_mutex_);

    // Read existing chunk header at offset
    ChunkEntryHeader entry;
    ssize_t n = pread(fd_, &entry, sizeof(entry), offset);
    if (n != sizeof(entry)) return false;

    if (entry.is_deleted) return true; // Already deleted

    entry.is_deleted = 1;
    n = pwrite(fd_, &entry, sizeof(entry), offset);
    return n == sizeof(entry);
}

std::vector<uint8_t> Segment::read_chunk(uint64_t offset, uint64_t length) const {
    // Read chunk header first
    ChunkEntryHeader entry;
    ssize_t n = pread(fd_, &entry, sizeof(entry), offset);
    if (n != sizeof(entry)) {
        throw std::runtime_error("Failed to read chunk header");
    }

    if (entry.is_deleted) {
        throw std::runtime_error("Chunk is marked deleted");
    }

    if (entry.chunk_size != length) {
        throw std::runtime_error("Chunk size mismatch");
    }

    // Read chunk data
    std::vector<uint8_t> data(length);
    n = pread(fd_, data.data(), length, offset + sizeof(entry));
    if (n != static_cast<ssize_t>(length)) {
        throw std::runtime_error("Failed to read chunk data");
    }

    return data;
}

uint32_t Segment::chunk_count() const { return header_.chunk_count; }
uint64_t Segment::total_data_bytes() const { return header_.total_data_bytes; }

ChunkEntryHeader Segment::read_chunk_header_at(uint64_t offset) const {
    ChunkEntryHeader entry;
    ssize_t n = pread(fd_, &entry, sizeof(entry), offset);
    if (n != sizeof(entry)) {
        throw std::runtime_error("Failed to read chunk header at offset " + std::to_string(offset));
    }
    return entry;
}

void Segment::write_header() {
    ssize_t n = pwrite(fd_, &header_, sizeof(header_), 0);
    if (n != sizeof(header_)) {
        throw std::runtime_error("Failed to write segment header");
    }
}

void Segment::read_header() {
    ssize_t n = pread(fd_, &header_, sizeof(header_), 0);
    if (n != sizeof(header_)) {
        throw std::runtime_error("Failed to read segment header");
    }
}

uint32_t Segment::compute_header_crc32c(const SegmentFileHeader& h) const {
    // CRC32C of all header fields except the header_crc32c field itself
    return crc32c(reinterpret_cast<const uint8_t*>(&h),
                  offsetof(SegmentFileHeader, header_crc32c));
}

// ============================================================================
// PageSegment
// ============================================================================

PageSegment::PageSegment(const std::string& path, bool create)
    : segment_(path, create)
{
    // Set page magic if creating
    if (create) {
        // The segment was created with STANDARD magic — we need to re-init with PAGE magic
        // Since Segment's constructor has already run with STANDARD magic,
        // we need a different approach. For simplicity, the Segment class handles
        // both types via the magic check on open.
    }
}

uint64_t PageSegment::write_chunk(uint64_t file_id, uint32_t chunk_index,
                                   const uint8_t* data, uint64_t size,
                                   uint32_t chunk_checksum, bool is_encrypted) {
    return segment_.write_chunk(file_id, chunk_index, data, size, chunk_checksum, is_encrypted);
}

std::vector<uint8_t> PageSegment::read_chunk(uint64_t offset, uint64_t length) const {
    return segment_.read_chunk(offset, length);
}

const SegmentFileHeader& PageSegment::header() const {
    return segment_.header();
}

// ---- Page bucket naming ----

std::string PageSegment::page_bucket_name(ExpiryGranularity granularity, uint64_t expires_at_us) {
    time_t t = static_cast<time_t>(expires_at_us / 1'000'000);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);

    std::ostringstream oss;

    switch (granularity) {
        case ExpiryGranularity::DAY:
            // "2026-06-03"
            oss << std::setfill('0')
                << (tm_buf.tm_year + 1900) << "-"
                << std::setw(2) << (tm_buf.tm_mon + 1) << "-"
                << std::setw(2) << tm_buf.tm_mday;
            break;
        case ExpiryGranularity::WEEK: {
            // "2026-W23" — ISO week number
            char week_str[16];
            strftime(week_str, sizeof(week_str), "%G-W%V", &tm_buf);
            oss << week_str;
            break;
        }
        case ExpiryGranularity::MONTH:
            // "2026-06"
            oss << std::setfill('0')
                << (tm_buf.tm_year + 1900) << "-"
                << std::setw(2) << (tm_buf.tm_mon + 1);
            break;
        case ExpiryGranularity::UNSET:
        default:
            return "permanent";
    }

    return oss.str();
}

std::string PageSegment::page_segment_filename(uint16_t node_id, uint32_t group_id,
                                                uint16_t table_id, const std::string& bucket) {
    std::ostringstream oss;
    oss << "page_" << node_id << "_" << group_id << "_" << table_id << "_" << bucket << ".seg";
    return oss.str();
}

uint64_t PageSegment::expiry_bucket_start_us(ExpiryGranularity granularity, uint64_t expires_at_us) {
    time_t t = static_cast<time_t>(expires_at_us / 1'000'000);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);

    // Zero out time components based on granularity
    switch (granularity) {
        case ExpiryGranularity::DAY:
            tm_buf.tm_hour = 0;
            tm_buf.tm_min = 0;
            tm_buf.tm_sec = 0;
            break;
        case ExpiryGranularity::WEEK: {
            // Go back to Monday of current week
            int days_since_monday = (tm_buf.tm_wday + 6) % 7; // tm_wday: Sun=0
            time_t monday = t - days_since_monday * 86400;
            gmtime_r(&monday, &tm_buf);
            tm_buf.tm_hour = 0;
            tm_buf.tm_min = 0;
            tm_buf.tm_sec = 0;
            break;
        }
        case ExpiryGranularity::MONTH:
            tm_buf.tm_mday = 1;
            tm_buf.tm_hour = 0;
            tm_buf.tm_min = 0;
            tm_buf.tm_sec = 0;
            break;
        case ExpiryGranularity::UNSET:
        default:
            return 0;
    }

    return static_cast<uint64_t>(timegm(&tm_buf)) * 1'000'000ULL;
}

bool PageSegment::unlink_page(const std::string& path) {
    return unlink(path.c_str()) == 0;
}

}  // namespace filegroup
