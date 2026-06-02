#include "manifest/manifest_writer.h"

#include "common/crc32c.h"
#include "common/clock.h"
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>

namespace filegroup {

ManifestWriter::ManifestWriter(const std::string& path, uint32_t group_id)
    : path_(path), group_id_(group_id), next_lsn_(1) {
    // Open with low-level file API for better control
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        throw std::runtime_error("Failed to open manifest file: " + path);
    }
    
    // Don't use ofstream — just keep fd as private member
    // Will write via write() system call or refactor
    close(fd);
    
    file_.open(path, std::ios::binary | std::ios::app);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open manifest file: " + path);
    }

    // TODO: Read existing manifest to determine next LSN
    // For now, start from 1
}

ManifestWriter::~ManifestWriter() {
    if (file_.is_open()) {
        file_.close();
    }
}

uint64_t ManifestWriter::append(ManifestEntryType entry_type, const void* body, uint16_t body_length) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Compute CRC32C of body
    uint32_t body_crc = crc32c(static_cast<const uint8_t*>(body), body_length);

    // Build header
    ManifestEntryHeader header;
    header.entry_lsn = next_lsn_;
    header.timestamp_us = now_us();
    header.crc32c = body_crc;
    header.entry_type = static_cast<uint16_t>(entry_type);
    header.length = body_length;

    // Write header
    file_.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write body
    file_.write(static_cast<const char*>(body), body_length);

    // Flush to disk
    file_.flush();
    // TODO: Add proper fsync() call once file descriptor handling is finalized

    uint64_t lsn = next_lsn_;
    next_lsn_++;

    return lsn;
}

uint64_t ManifestWriter::current_lsn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_lsn_;
}

}  // namespace filegroup
