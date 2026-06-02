#include "segment/segment_writer.h"

#include "common/crc32c.h"
#include "common/clock.h"
#include <cstring>
#include <stdexcept>

namespace filegroup {

SegmentWriter::SegmentWriter(const std::string& path, SegmentType segment_type)
    : path_(path), segment_type_(segment_type), total_size_(0), finalized_(false) {
    file_.open(path, std::ios::binary);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open segment file: " + path);
    }

    // Write placeholder header (will be updated on finalize)
    SegmentHeader header;
    std::memset(&header, 0, sizeof(header));
    header.magic = SEGMENT_MAGIC;
    header.format_version = SEGMENT_FORMAT_VERSION;
    header.segment_type = static_cast<uint8_t>(segment_type);
    header.created_at_us = now_us();

    file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!file_.good()) {
        throw std::runtime_error("Failed to write segment header");
    }
}

SegmentWriter::~SegmentWriter() {
    if (file_.is_open()) {
        file_.close();
    }
}

uint64_t SegmentWriter::append_chunk(uint32_t chunk_index, const void* data, uint64_t size, uint8_t replication_factor) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (finalized_) {
        throw std::runtime_error("Segment already finalized");
    }

    // Compute checksum
    uint32_t checksum = crc32c(static_cast<const uint8_t*>(data), size);

    // Write chunk entry
    ChunkEntry entry;
    entry.chunk_index = chunk_index;
    entry.chunk_size = size;
    entry.chunk_checksum = checksum;
    entry.replication_factor = replication_factor;

    uint64_t offset = file_.tellp();
    file_.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    file_.write(static_cast<const char*>(data), size);

    if (!file_.good()) {
        throw std::runtime_error("Failed to write chunk");
    }

    chunks_.push_back(entry);
    total_size_ += size;

    return offset;
}

void SegmentWriter::finalize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (finalized_) {
        return;  // Already finalized
    }

    // Write updated header at beginning of file
    file_.seekp(0);

    SegmentHeader header;
    header.magic = SEGMENT_MAGIC;
    header.format_version = SEGMENT_FORMAT_VERSION;
    header.segment_type = static_cast<uint8_t>(segment_type_);
    header.chunk_count = chunks_.size();
    header.created_at_us = now_us();
    header.total_size = total_size_;

    file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file_.flush();

    finalized_ = true;
}

uint32_t SegmentWriter::chunk_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunks_.size();
}

uint64_t SegmentWriter::total_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_size_;
}

}  // namespace filegroup
