#pragma once

#include <cstddef>
#include <cstdint>

namespace filegroup {

/**
 * Compute CRC32C (Castagnoli) checksum of data.
 * Uses hardware acceleration (__builtin_ia32_crc32*) if available, otherwise software fallback.
 *
 * @param data pointer to data buffer
 * @param len length in bytes
 * @return CRC32C checksum
 */
uint32_t crc32c(const uint8_t* data, size_t len);

/**
 * CRC32C class for incremental computation.
 */
class CRC32C {
public:
    CRC32C();
    ~CRC32C() = default;

    /** Update with additional data */
    void update(const uint8_t* data, size_t len);

    /** Get current CRC32C value */
    uint32_t finalize() const;

    /** Reset to initial state */
    void reset();

private:
    uint32_t crc_;
};

}  // namespace filegroup
