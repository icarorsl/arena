#pragma once

#include <cstdint>
#include <cstring>

#include "common/types.h"

namespace filegroup {

/**
 * 64-byte file header — binary layout (little-endian).
 * Identifies the file and its content metadata.
 */
#pragma pack(push, 1)
struct FileHeader {
    uint8_t  file_header_version;  // Must be 0x01
    uint64_t logical_file_id;
    uint64_t file_id;
    uint32_t version_number;
    uint16_t table_id;
    uint32_t group_id;
    uint64_t total_size;           // Total file size in bytes
    uint32_t chunk_count;
    uint64_t chunk_size;           // Per-chunk size in bytes
    uint8_t  replication_factor;
    uint64_t expires_at;           // Unix microseconds, 0 = no expiry
    uint8_t  encryption;           // EncryptionAlgo value (0x00 = NONE, 0x01 = AES_256_GCM)
    uint32_t content_checksum;     // CRC32C of full file plaintext
    uint8_t  reserved[3];          // Must be zero
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 64, "FileHeader must be exactly 64 bytes");
static constexpr uint8_t FILE_HEADER_VERSION = 0x01;

/**
 * Serialize FileHeader to 64-byte buffer (little-endian).
 * @param header header to serialize
 * @param buf output buffer (must be at least 64 bytes)
 */
void serialize_file_header(const FileHeader& header, uint8_t* buf);

/**
 * Deserialize FileHeader from 64-byte buffer (little-endian).
 * @param buf input buffer (must be at least 64 bytes)
 * @return deserialized FileHeader
 * @throws std::runtime_error if version is unknown
 */
FileHeader deserialize_file_header(const uint8_t* buf);

/**
 * Validate FileHeader contents.
 * Checks:
 * - version == 0x01
 * - reserved bytes == 0
 * - chunk_size > 0
 *
 * @param header header to validate
 * @return true if valid, false otherwise
 */
bool validate_file_header(const FileHeader& header);

}  // namespace filegroup
