#include <gtest/gtest.h>
#include <filesystem>
#include <cstring>

#include "segment/segment_writer.h"
#include "segment/segment_reader.h"
#include "common/crc32c.h"

namespace filegroup {

class SegmentTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/segment_test_" + std::to_string(rand());
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
};

// Test: Create and write standard segment
TEST_F(SegmentTest, WriteStandardSegment) {
    std::string path = test_dir_ + "/segment.bin";
    
    SegmentWriter writer(path, SegmentType::STANDARD);
    
    uint8_t data1[100];
    std::memset(data1, 0xAA, sizeof(data1));
    
    uint64_t offset1 = writer.append_chunk(0, data1, sizeof(data1), 3);
    EXPECT_GT(offset1, 0);
    EXPECT_EQ(writer.chunk_count(), 1);
    EXPECT_EQ(writer.total_size(), 100);
    
    writer.finalize();
}

// Test: Write multiple chunks
TEST_F(SegmentTest, WriteMultipleChunks) {
    std::string path = test_dir_ + "/segment.bin";
    
    SegmentWriter writer(path, SegmentType::STANDARD);
    
    for (int i = 0; i < 5; i++) {
        uint8_t data[200];
        std::memset(data, 0xAA + i, sizeof(data));
        writer.append_chunk(i, data, sizeof(data), 2);
    }
    
    EXPECT_EQ(writer.chunk_count(), 5);
    EXPECT_EQ(writer.total_size(), 1000);
    
    writer.finalize();
}

// Test: Write and read round-trip
TEST_F(SegmentTest, RoundTripWriteRead) {
    std::string path = test_dir_ + "/segment.bin";
    
    // Write
    {
        SegmentWriter writer(path, SegmentType::STANDARD);
        
        uint8_t data[256];
        std::memset(data, 0xBB, sizeof(data));
        
        writer.append_chunk(0, data, sizeof(data), 3);
        writer.finalize();
    }
    
    // Read
    SegmentReader reader(path);
    
    EXPECT_EQ(reader.header().magic, SEGMENT_MAGIC);
    EXPECT_EQ(reader.header().format_version, SEGMENT_FORMAT_VERSION);
    EXPECT_EQ(reader.header().segment_type, static_cast<uint8_t>(SegmentType::STANDARD));
    EXPECT_EQ(reader.header().chunk_count, 1);
    EXPECT_EQ(reader.header().total_size, 256);
    
    EXPECT_EQ(reader.chunks().size(), 1);
    EXPECT_EQ(reader.chunks()[0].chunk_index, 0);
    EXPECT_EQ(reader.chunks()[0].data.size(), 256);
}

// Test: Verify chunk data integrity
TEST_F(SegmentTest, ChunkDataIntegrity) {
    std::string path = test_dir_ + "/segment.bin";
    
    uint8_t original[512];
    for (int i = 0; i < 512; i++) {
        original[i] = (i * 7) % 256;
    }
    
    // Write
    {
        SegmentWriter writer(path, SegmentType::STANDARD);
        writer.append_chunk(0, original, sizeof(original), 1);
        writer.finalize();
    }
    
    // Read and verify
    SegmentReader reader(path);
    
    const SegmentChunk* chunk = reader.get_chunk(0);
    ASSERT_NE(chunk, nullptr);
    
    ASSERT_EQ(chunk->data.size(), sizeof(original));
    EXPECT_EQ(std::memcmp(chunk->data.data(), original, sizeof(original)), 0);
}

// Test: Checksum verification
TEST_F(SegmentTest, ChecksumVerification) {
    std::string path = test_dir_ + "/segment.bin";
    
    uint8_t data[128];
    std::memset(data, 0xCC, sizeof(data));
    
    // Write
    {
        SegmentWriter writer(path, SegmentType::STANDARD);
        writer.append_chunk(5, data, sizeof(data), 2);
        writer.finalize();
    }
    
    // Read
    SegmentReader reader(path);
    
    EXPECT_TRUE(reader.verify_checksums());
    
    const SegmentChunk* chunk = reader.get_chunk(5);
    ASSERT_NE(chunk, nullptr);
    
    uint32_t computed = crc32c(chunk->data.data(), chunk->data.size());
    EXPECT_EQ(computed, chunk->checksum);
}

// Test: Get chunk by index
TEST_F(SegmentTest, GetChunkByIndex) {
    std::string path = test_dir_ + "/segment.bin";
    
    // Write multiple chunks with specific indices
    {
        SegmentWriter writer(path, SegmentType::STANDARD);
        
        for (int i = 0; i < 10; i += 2) {  // 0, 2, 4, 6, 8
            uint8_t data[64];
            std::memset(data, 0xDD + i, sizeof(data));
            writer.append_chunk(i, data, sizeof(data), 1);
        }
        
        writer.finalize();
    }
    
    // Read and find specific chunks
    SegmentReader reader(path);
    
    EXPECT_NE(reader.get_chunk(0), nullptr);
    EXPECT_NE(reader.get_chunk(2), nullptr);
    EXPECT_NE(reader.get_chunk(8), nullptr);
    EXPECT_EQ(reader.get_chunk(1), nullptr);  // Not written
    EXPECT_EQ(reader.get_chunk(9), nullptr);  // Not written
}

// Test: Page segment type
TEST_F(SegmentTest, PageSegmentType) {
    std::string path = test_dir_ + "/segment.bin";
    
    // Write with PAGE type
    {
        SegmentWriter writer(path, SegmentType::PAGE);
        
        uint8_t data[256];
        std::memset(data, 0xEE, sizeof(data));
        
        writer.append_chunk(0, data, sizeof(data), 1);
        writer.finalize();
    }
    
    // Read and verify type
    SegmentReader reader(path);
    
    EXPECT_EQ(reader.header().segment_type, static_cast<uint8_t>(SegmentType::PAGE));
}

// Test: Chunk count matches
TEST_F(SegmentTest, ChunkCountMatches) {
    std::string path = test_dir_ + "/segment.bin";
    
    const int CHUNK_COUNT = 20;
    
    // Write
    {
        SegmentWriter writer(path, SegmentType::STANDARD);
        
        for (int i = 0; i < CHUNK_COUNT; i++) {
            uint8_t data[100];
            std::memset(data, i, sizeof(data));
            writer.append_chunk(i, data, sizeof(data), 1);
        }
        
        EXPECT_EQ(writer.chunk_count(), CHUNK_COUNT);
        writer.finalize();
    }
    
    // Read
    SegmentReader reader(path);
    
    EXPECT_EQ(reader.header().chunk_count, CHUNK_COUNT);
    EXPECT_EQ(reader.chunks().size(), CHUNK_COUNT);
}

// Test: Large chunks
TEST_F(SegmentTest, LargeChunks) {
    std::string path = test_dir_ + "/segment.bin";
    
    const size_t LARGE_SIZE = 10 * 1024 * 1024;  // 10MB
    
    // Write
    {
        SegmentWriter writer(path, SegmentType::STANDARD);
        
        std::vector<uint8_t> data(LARGE_SIZE);
        std::fill(data.begin(), data.end(), 0x42);
        
        writer.append_chunk(0, data.data(), data.size(), 1);
        EXPECT_EQ(writer.total_size(), LARGE_SIZE);
        
        writer.finalize();
    }
    
    // Read
    SegmentReader reader(path);
    
    EXPECT_EQ(reader.header().total_size, LARGE_SIZE);
    
    const SegmentChunk* chunk = reader.get_chunk(0);
    ASSERT_NE(chunk, nullptr);
    EXPECT_EQ(chunk->data.size(), LARGE_SIZE);
}

// Test: SegmentHeader size
TEST(SegmentTypes, HeaderSize) {
    EXPECT_EQ(sizeof(SegmentHeader), 36);
}

// Test: ChunkEntry size
TEST(SegmentTypes, ChunkEntrySize) {
    EXPECT_EQ(sizeof(ChunkEntry), 20);
}

}  // namespace filegroup
