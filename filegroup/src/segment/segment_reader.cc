#include "segment/segment_reader.h"

#include "common/crc32c.h"
#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace filegroup {

SegmentReader::SegmentReader(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open segment file: " + path);
    }

    // Read header
    file.read(reinterpret_cast<char*>(&header_), sizeof(header_));
    if (file.gcount() != sizeof(header_)) {
        throw std::runtime_error("Failed to read segment header");
    }

    // Validate magic
    if (header_.magic != SEGMENT_MAGIC) {
        throw std::runtime_error("Invalid segment magic: 0x" + std::to_string(header_.magic));
    }

    // Validate format version
    if (header_.format_version != SEGMENT_FORMAT_VERSION) {
        throw std::runtime_error("Unsupported segment format version");
    }

    // Read chunks
    for (uint32_t i = 0; i < header_.chunk_count; i++) {
        ChunkEntry entry;
        file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        if (file.gcount() != sizeof(entry)) {
            throw std::runtime_error("Failed to read chunk entry");
        }

        std::vector<uint8_t> data(entry.chunk_size);
        file.read(reinterpret_cast<char*>(data.data()), entry.chunk_size);
        if (file.gcount() != static_cast<int>(entry.chunk_size)) {
            throw std::runtime_error("Failed to read chunk data");
        }

        SegmentChunk chunk;
        chunk.chunk_index = entry.chunk_index;
        chunk.data = std::move(data);
        chunk.checksum = entry.chunk_checksum;
        chunk.replication_factor = entry.replication_factor;

        chunks_.push_back(chunk);
    }
}

const SegmentChunk* SegmentReader::get_chunk(uint32_t chunk_index) const {
    auto it = std::find_if(chunks_.begin(), chunks_.end(),
        [chunk_index](const SegmentChunk& c) { return c.chunk_index == chunk_index; });
    return it != chunks_.end() ? &(*it) : nullptr;
}

bool SegmentReader::verify_checksums() const {
    for (const auto& chunk : chunks_) {
        uint32_t computed = crc32c(chunk.data.data(), chunk.data.size());
        if (computed != chunk.checksum) {
            return false;
        }
    }
    return true;
}

}  // namespace filegroup
