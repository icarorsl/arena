#include <gtest/gtest.h>

#include "file_header/file_header.h"
#include <cstring>

using namespace filegroup;

class FileHeaderTest : public ::testing::Test {
protected:
    FileHeader create_valid_header() {
        FileHeader h;
        h.file_header_version = FILE_HEADER_VERSION;
        h.logical_file_id = 123456789;
        h.file_id = 987654321;
        h.version_number = 1;
        h.table_id = 101;
        h.group_id = 1;
        h.total_size = 1000000;
        h.chunk_count = 16;
        h.chunk_size = 65536;
        h.replication_factor = 3;
        h.expires_at = 0;  // No expiry
        h.encryption = static_cast<uint8_t>(EncryptionAlgo::AES_256_GCM);
        h.content_checksum = 0xdeadbeef;
        h.reserved[0] = 0;
        h.reserved[1] = 0;
        h.reserved[2] = 0;
        return h;
    }
};

TEST_F(FileHeaderTest, SizeIs64Bytes) {
    EXPECT_EQ(sizeof(FileHeader), 64u);
}

TEST_F(FileHeaderTest, SerializeAndDeserialize) {
    FileHeader original = create_valid_header();
    uint8_t buf[64];
    
    serialize_file_header(original, buf);
    FileHeader deserialized = deserialize_file_header(buf);
    
    EXPECT_EQ(deserialized.file_header_version, original.file_header_version);
    EXPECT_EQ(deserialized.logical_file_id, original.logical_file_id);
    EXPECT_EQ(deserialized.file_id, original.file_id);
    EXPECT_EQ(deserialized.version_number, original.version_number);
    EXPECT_EQ(deserialized.table_id, original.table_id);
    EXPECT_EQ(deserialized.group_id, original.group_id);
    EXPECT_EQ(deserialized.total_size, original.total_size);
    EXPECT_EQ(deserialized.chunk_count, original.chunk_count);
    EXPECT_EQ(deserialized.chunk_size, original.chunk_size);
    EXPECT_EQ(deserialized.replication_factor, original.replication_factor);
    EXPECT_EQ(deserialized.expires_at, original.expires_at);
    EXPECT_EQ(deserialized.encryption, original.encryption);
    EXPECT_EQ(deserialized.content_checksum, original.content_checksum);
}

TEST_F(FileHeaderTest, RoundTripMultipleHeaders) {
    // Create multiple headers with different values
    for (uint64_t i = 0; i < 10; ++i) {
        FileHeader original = create_valid_header();
        original.file_id = 1000000 + i;
        original.logical_file_id = 2000000 + i;
        original.chunk_count = i + 1;
        
        uint8_t buf[64];
        serialize_file_header(original, buf);
        FileHeader deserialized = deserialize_file_header(buf);
        
        EXPECT_EQ(deserialized.file_id, original.file_id);
        EXPECT_EQ(deserialized.logical_file_id, original.logical_file_id);
        EXPECT_EQ(deserialized.chunk_count, original.chunk_count);
    }
}

TEST_F(FileHeaderTest, ValidateValidHeader) {
    FileHeader header = create_valid_header();
    EXPECT_TRUE(validate_file_header(header));
}

TEST_F(FileHeaderTest, ValidateInvalidVersionRejected) {
    FileHeader header = create_valid_header();
    header.file_header_version = 0x02;  // Unknown version
    EXPECT_FALSE(validate_file_header(header));
}

TEST_F(FileHeaderTest, ValidateReservedBytesNonZeroRejected) {
    FileHeader header = create_valid_header();
    header.reserved[0] = 0xFF;
    EXPECT_FALSE(validate_file_header(header));
    
    header = create_valid_header();
    header.reserved[1] = 0x01;
    EXPECT_FALSE(validate_file_header(header));
    
    header = create_valid_header();
    header.reserved[2] = 0x42;
    EXPECT_FALSE(validate_file_header(header));
}

TEST_F(FileHeaderTest, ValidateZeroChunkSizeRejected) {
    FileHeader header = create_valid_header();
    header.chunk_size = 0;
    EXPECT_FALSE(validate_file_header(header));
}

TEST_F(FileHeaderTest, ValidateZeroReplicationFactorRejected) {
    FileHeader header = create_valid_header();
    header.replication_factor = 0;
    EXPECT_FALSE(validate_file_header(header));
}

TEST_F(FileHeaderTest, DeserializeUnknownVersionThrows) {
    uint8_t buf[64];
    std::memset(buf, 0, 64);
    buf[0] = 0xFF;  // Invalid version
    
    EXPECT_THROW(deserialize_file_header(buf), std::runtime_error);
}

TEST_F(FileHeaderTest, DeserializeKnownVersionSucceeds) {
    FileHeader original = create_valid_header();
    uint8_t buf[64];
    serialize_file_header(original, buf);
    
    // This should not throw
    EXPECT_NO_THROW(deserialize_file_header(buf));
}

TEST_F(FileHeaderTest, BufferBoundaryValues) {
    FileHeader header = create_valid_header();
    
    // Test with max values
    header.logical_file_id = UINT64_MAX;
    header.file_id = UINT64_MAX;
    header.total_size = UINT64_MAX;
    header.chunk_size = UINT64_MAX;
    header.expires_at = UINT64_MAX;
    header.group_id = UINT32_MAX;
    header.version_number = UINT32_MAX;
    header.chunk_count = UINT32_MAX;
    header.content_checksum = UINT32_MAX;
    header.table_id = UINT16_MAX;
    
    uint8_t buf[64];
    serialize_file_header(header, buf);
    FileHeader deserialized = deserialize_file_header(buf);
    
    EXPECT_EQ(deserialized.logical_file_id, UINT64_MAX);
    EXPECT_EQ(deserialized.file_id, UINT64_MAX);
    EXPECT_EQ(deserialized.total_size, UINT64_MAX);
    EXPECT_EQ(deserialized.chunk_size, UINT64_MAX);
}

TEST_F(FileHeaderTest, EncryptionAlgoBytesPreserved) {
    FileHeader header = create_valid_header();
    
    // Test NONE
    header.encryption = static_cast<uint8_t>(EncryptionAlgo::NONE);
    uint8_t buf[64];
    serialize_file_header(header, buf);
    FileHeader deserialized = deserialize_file_header(buf);
    EXPECT_EQ(deserialized.encryption, static_cast<uint8_t>(EncryptionAlgo::NONE));
    
    // Test AES_256_GCM
    header.encryption = static_cast<uint8_t>(EncryptionAlgo::AES_256_GCM);
    serialize_file_header(header, buf);
    deserialized = deserialize_file_header(buf);
    EXPECT_EQ(deserialized.encryption, static_cast<uint8_t>(EncryptionAlgo::AES_256_GCM));
}
