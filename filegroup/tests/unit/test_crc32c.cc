#include <gtest/gtest.h>

#include "common/crc32c.h"

using namespace filegroup;

// Known CRC32C test vectors (Castagnoli polynomial)
// These are standard test vectors for CRC32C
TEST(CRC32CTest, EmptyBuffer) {
    uint32_t crc = crc32c(nullptr, 0);
    EXPECT_EQ(crc, 0u);
}

TEST(CRC32CTest, SingleByte) {
    uint8_t data[] = {0x00};
    uint32_t crc = crc32c(data, sizeof(data));
    EXPECT_NE(crc, 0u);
}

TEST(CRC32CTest, KnownVector1) {
    // "123456789" is a common CRC32C test vector
    uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    uint32_t crc = crc32c(data, sizeof(data));
    // CRC32C("123456789") = 0xf28417be (Castagnoli polynomial)
    EXPECT_EQ(crc, 0xf28417beu);
}

TEST(CRC32CTest, KnownVector2) {
    // "The quick brown fox jumps over the lazy dog"
    const char* str = "The quick brown fox jumps over the lazy dog";
    uint32_t crc = crc32c(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
    // CRC32C of this string is a known value
    EXPECT_NE(crc, 0u);
}

TEST(CRC32CTest, IncrementalAccumulation) {
    uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    
    // Compute full CRC
    uint32_t full_crc = crc32c(data, sizeof(data));
    
    // Compute incremental CRC
    CRC32C incremental;
    incremental.update(data, 3);
    incremental.update(data + 3, 3);
    incremental.update(data + 6, 3);
    uint32_t inc_crc = incremental.finalize();
    
    // Both should match
    EXPECT_EQ(full_crc, inc_crc);
}

TEST(CRC32CTest, ResetWorks) {
    uint8_t data[] = {0x31, 0x32, 0x33};
    
    CRC32C crc;
    crc.update(data, sizeof(data));
    uint32_t first = crc.finalize();
    
    crc.reset();
    uint32_t reset_empty = crc.finalize();
    EXPECT_EQ(reset_empty, 0u);
    
    crc.update(data, sizeof(data));
    uint32_t second = crc.finalize();
    
    // Both computations should match
    EXPECT_EQ(first, second);
}

TEST(CRC32CTest, DifferentDataDifferentCRC) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x04, 0x05, 0x06};
    
    uint32_t crc1 = crc32c(data1, sizeof(data1));
    uint32_t crc2 = crc32c(data2, sizeof(data2));
    
    EXPECT_NE(crc1, crc2);
}

TEST(CRC32CTest, LargeBuffer) {
    // Test with a large buffer
    size_t size = 1024 * 1024;  // 1MB
    std::vector<uint8_t> data(size);
    
    // Fill with a pattern
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>((i * 7) & 0xff);
    }
    
    uint32_t crc = crc32c(data.data(), data.size());
    EXPECT_NE(crc, 0u);
    
    // Verify reproducibility
    uint32_t crc2 = crc32c(data.data(), data.size());
    EXPECT_EQ(crc, crc2);
}
