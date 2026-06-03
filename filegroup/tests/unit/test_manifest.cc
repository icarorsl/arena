#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstring>

#include "manifest/manifest_writer.h"
#include "manifest/manifest_reader.h"
#include "manifest/file_index.h"
#include "common/crc32c.h"

namespace filegroup {

class ManifestTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/manifest_test_" + std::to_string(rand());
        std::filesystem::create_directories(test_dir_);
        manifest_path_ = test_dir_ + "/manifest.log";
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
    std::string manifest_path_;
};

// Test: Create manifest writer and append a single entry
TEST_F(ManifestTest, BasicWrite) {
    ManifestWriter writer(manifest_path_, 1);
    
    SessionOpenEntry entry;
    entry.session_id = 1001;
    entry.file_id = 101;
    entry.logical_file_id = 100001;
    entry.table_id = 1;
    entry.group_id = 1;
    entry.version_number = 1;
    entry.chunk_size = 4096;
    entry.replication_factor = 3;
    entry.expected_chunks = 10;
    entry.expires_at = 1000000000ULL;
    entry.encryption = static_cast<uint8_t>(EncryptionAlgo::AES_256_GCM);
    entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    uint64_t lsn = writer.append(ManifestEntryType::SESSION_OPEN, &entry, sizeof(entry));
    EXPECT_EQ(lsn, 1);
    EXPECT_EQ(writer.current_lsn(), 2);
}

// Test: Write multiple entries and verify LSN sequence
TEST_F(ManifestTest, MultipleWrites) {
    ManifestWriter writer(manifest_path_, 1);
    
    SessionOpenEntry session_entry;
    session_entry.session_id = 1001;
    uint64_t lsn1 = writer.append(ManifestEntryType::SESSION_OPEN, &session_entry, sizeof(session_entry));
    EXPECT_EQ(lsn1, 1);

    ChunkConfirmedEntry chunk_entry;
    chunk_entry.session_id = 1001;
    chunk_entry.chunk_index = 0;
    chunk_entry.chunk_size_actual = 4096;
    chunk_entry.chunk_checksum = 12345;
    uint64_t lsn2 = writer.append(ManifestEntryType::CHUNK_CONFIRMED, &chunk_entry, sizeof(chunk_entry));
    EXPECT_EQ(lsn2, 2);

    VersionCompleteEntry version_entry;
    version_entry.file_id = 101;
    version_entry.logical_file_id = 100001;
    version_entry.version_number = 1;
    version_entry.chunk_count = 1;
    version_entry.content_checksum = 12345;
    uint64_t lsn3 = writer.append(ManifestEntryType::VERSION_COMPLETE, &version_entry, sizeof(version_entry));
    EXPECT_EQ(lsn3, 3);

    EXPECT_EQ(writer.current_lsn(), 4);
}

// Test: Read manifest with round-trip verification
TEST_F(ManifestTest, RoundTripWrite) {
    // Write entries
    {
        ManifestWriter writer(manifest_path_, 1);
        
        SessionOpenEntry entry;
        entry.session_id = 5001;
        entry.file_id = 501;
        entry.chunk_size = 8192;
        entry.replication_factor = 2;
        writer.append(ManifestEntryType::SESSION_OPEN, &entry, sizeof(entry));
    }

    // Read manifest file and verify
    std::ifstream file(manifest_path_, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    // Read header
    ManifestEntryHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    ASSERT_EQ(file.gcount(), sizeof(header));

    EXPECT_EQ(header.entry_lsn, 1);
    EXPECT_EQ(header.entry_type, static_cast<uint16_t>(ManifestEntryType::SESSION_OPEN));
    EXPECT_EQ(header.length, sizeof(SessionOpenEntry));

    // Read body
    SessionOpenEntry entry;
    file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
    ASSERT_EQ(file.gcount(), sizeof(entry));

    EXPECT_EQ(entry.session_id, 5001);
    EXPECT_EQ(entry.file_id, 501);
    EXPECT_EQ(entry.chunk_size, 8192);
    EXPECT_EQ(entry.replication_factor, 2);

    // Verify CRC32C
    uint32_t computed_crc = crc32c(reinterpret_cast<uint8_t*>(&entry), sizeof(entry));
    EXPECT_EQ(header.crc32c, computed_crc);
}

// Test: Verify timestamp is set in header
TEST_F(ManifestTest, TimestampInHeader) {
    ManifestWriter writer(manifest_path_, 1);
    
    SessionOpenEntry entry;
    writer.append(ManifestEntryType::SESSION_OPEN, &entry, sizeof(entry));

    std::ifstream file(manifest_path_, std::ios::binary);
    ManifestEntryHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    // Timestamp should be non-zero and relatively recent
    EXPECT_GT(header.timestamp_us, 0);
    EXPECT_GT(header.timestamp_us, 1600000000000000ULL);  // After 2020
}

// Test: File index with node health
TEST_F(ManifestTest, FileIndexNodeHealth) {
    FileIndex index;
    
    NodeHealthEntry entry;
    entry.node_id = 10;
    entry.state = static_cast<uint8_t>(NodeState::HEALTHY);
    index.apply_node_health(entry);

    // TODO: Add getter method to test
}

// Test: ID generation in FileIndex
TEST_F(ManifestTest, FileIndexIDGeneration) {
    FileIndex index;
    
    uint64_t id1 = index.next_logical_file_id();
    uint64_t id2 = index.next_logical_file_id();
    uint64_t id3 = index.next_logical_file_id();

    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);
    EXPECT_EQ(id3, 3);
    
    uint64_t session1 = index.next_session_id();
    uint64_t session2 = index.next_session_id();
    EXPECT_EQ(session1, 1);
    EXPECT_EQ(session2, 2);
}

// Test: Large batch write/read
TEST_F(ManifestTest, LargeBatch) {
    const int NUM_ENTRIES = 100;
    
    // Write 100 entries
    {
        ManifestWriter writer(manifest_path_, 1);
        
        for (int i = 0; i < NUM_ENTRIES; i++) {
            ChunkConfirmedEntry entry;
            entry.session_id = 1000 + i;
            entry.chunk_index = i;
            entry.chunk_size_actual = 4096 * (i + 1);
            writer.append(ManifestEntryType::CHUNK_CONFIRMED, &entry, sizeof(entry));
        }
    }

    // Read and verify file size
    std::ifstream file(manifest_path_, std::ios::binary);
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    
    // Each entry: 24-byte header + sizeof(ChunkConfirmedEntry)
    size_t expected_size = NUM_ENTRIES * (sizeof(ManifestEntryHeader) + sizeof(ChunkConfirmedEntry));
    EXPECT_EQ(file_size, expected_size);
}

// Test: CRC32C validation in round-trip
TEST_F(ManifestTest, CRCValidation) {
    // Write entry with known data
    {
        ManifestWriter writer(manifest_path_, 1);
        
        ChunkConfirmedEntry entry;
        std::memset(&entry, 0xFF, sizeof(entry));
        entry.session_id = 9999;
        entry.chunk_index = 77;
        writer.append(ManifestEntryType::CHUNK_CONFIRMED, &entry, sizeof(entry));
    }

    // Verify CRC in file
    std::ifstream file(manifest_path_, std::ios::binary);
    ManifestEntryHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    ChunkConfirmedEntry entry;
    file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
    
    // Recompute CRC and verify
    uint32_t expected_crc = crc32c(reinterpret_cast<uint8_t*>(&entry), sizeof(entry));
    EXPECT_EQ(header.crc32c, expected_crc);
}

// Test: Different entry types
TEST_F(ManifestTest, DifferentEntryTypes) {
    ManifestWriter writer(manifest_path_, 1);
    
    // Write different entry types
    SessionOpenEntry session_entry;
    writer.append(ManifestEntryType::SESSION_OPEN, &session_entry, sizeof(session_entry));
    
    ChunkConfirmedEntry chunk_entry;
    writer.append(ManifestEntryType::CHUNK_CONFIRMED, &chunk_entry, sizeof(chunk_entry));
    
    VersionCompleteEntry version_entry;
    writer.append(ManifestEntryType::VERSION_COMPLETE, &version_entry, sizeof(version_entry));
    
    VersionDeletedEntry deleted_entry;
    writer.append(ManifestEntryType::VERSION_DELETED, &deleted_entry, sizeof(deleted_entry));
    
    FileDeletedEntry file_deleted_entry;
    writer.append(ManifestEntryType::FILE_DELETED, &file_deleted_entry, sizeof(file_deleted_entry));
    
    NodeHealthEntry health_entry;
    writer.append(ManifestEntryType::NODE_HEALTH, &health_entry, sizeof(health_entry));

    // Read and verify types
    std::ifstream file(manifest_path_, std::ios::binary);
    uint16_t expected_types[] = {
        static_cast<uint16_t>(ManifestEntryType::SESSION_OPEN),
        static_cast<uint16_t>(ManifestEntryType::CHUNK_CONFIRMED),
        static_cast<uint16_t>(ManifestEntryType::VERSION_COMPLETE),
        static_cast<uint16_t>(ManifestEntryType::VERSION_DELETED),
        static_cast<uint16_t>(ManifestEntryType::FILE_DELETED),
        static_cast<uint16_t>(ManifestEntryType::NODE_HEALTH),
    };
    
    for (uint16_t expected_type : expected_types) {
        ManifestEntryHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        ASSERT_TRUE(file.good());
        
        EXPECT_EQ(header.entry_type, expected_type);
        
        // Skip body
        file.seekg(header.length, std::ios::cur);
    }
}

}  // namespace filegroup
