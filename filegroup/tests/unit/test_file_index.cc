#include <gtest/gtest.h>
#include "manifest/file_index.h"

namespace filegroup {

class FileIndexTest : public ::testing::Test {
protected:
    FileIndex index;
};

// Test: Session open creates file and version
TEST_F(FileIndexTest, SessionOpenCreatesEntry) {
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

    index.apply_session_open(entry);

    const LogicalFileEntry* file = index.get_file(100001);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->logical_file_id, 100001);
    EXPECT_EQ(file->table_id, 1);
    EXPECT_EQ(file->group_id, 1);

    const VersionEntry* ver = index.get_version(100001, 1);
    ASSERT_NE(ver, nullptr);
    EXPECT_EQ(ver->version_number, 1);
    EXPECT_EQ(ver->state, VersionState::UPLOADING);
    EXPECT_EQ(ver->chunk_count, 10);
}

// Test: Chunk confirmed adds chunk to version
TEST_F(FileIndexTest, ChunkConfirmedAddsChunk) {
    SessionOpenEntry session_entry;
    session_entry.session_id = 2001;
    session_entry.logical_file_id = 200001;
    session_entry.file_id = 201;
    session_entry.table_id = 2;
    session_entry.group_id = 2;
    session_entry.version_number = 1;
    session_entry.chunk_size = 4096;
    session_entry.replication_factor = 2;
    session_entry.expected_chunks = 5;
    session_entry.expires_at = 1000000000ULL;
    session_entry.encryption = static_cast<uint8_t>(EncryptionAlgo::AES_256_GCM);
    session_entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    index.apply_session_open(session_entry);

    ChunkConfirmedEntry chunk_entry;
    chunk_entry.session_id = 2001;
    chunk_entry.file_id = 201;
    chunk_entry.chunk_index = 0;
    chunk_entry.chunk_size_actual = 4096;
    chunk_entry.chunk_checksum = 11111;
    chunk_entry.replica_count = 2;

    index.apply_chunk_confirmed(chunk_entry);

    const VersionEntry* ver = index.get_version(200001, 1);
    ASSERT_NE(ver, nullptr);
    EXPECT_EQ(ver->chunks.size(), 1);
    EXPECT_EQ(ver->chunks[0].chunk_index, 0);
    EXPECT_EQ(ver->chunks[0].chunk_size_actual, 4096);
    EXPECT_EQ(ver->chunks[0].chunk_checksum, 11111);
    EXPECT_EQ(ver->total_size, 4096);
}

// Test: Multiple chunks
TEST_F(FileIndexTest, MultipleChunks) {
    SessionOpenEntry session_entry;
    session_entry.session_id = 3001;
    session_entry.logical_file_id = 300001;
    session_entry.file_id = 301;
    session_entry.table_id = 3;
    session_entry.group_id = 3;
    session_entry.version_number = 1;
    session_entry.chunk_size = 4096;
    session_entry.replication_factor = 1;
    session_entry.expected_chunks = 3;
    session_entry.expires_at = 1000000000ULL;
    session_entry.encryption = static_cast<uint8_t>(EncryptionAlgo::NONE);
    session_entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    index.apply_session_open(session_entry);

    for (int i = 0; i < 3; i++) {
        ChunkConfirmedEntry chunk_entry;
        chunk_entry.session_id = 3001;
        chunk_entry.file_id = 301;
        chunk_entry.chunk_index = i;
        chunk_entry.chunk_size_actual = 4096;
        chunk_entry.chunk_checksum = 20000 + i;
        index.apply_chunk_confirmed(chunk_entry);
    }

    const VersionEntry* ver = index.get_version(300001, 1);
    ASSERT_NE(ver, nullptr);
    EXPECT_EQ(ver->chunks.size(), 3);
    EXPECT_EQ(ver->total_size, 12288);
}

// Test: Version complete transitions state
TEST_F(FileIndexTest, VersionComplete) {
    SessionOpenEntry session_entry;
    session_entry.session_id = 4001;
    session_entry.logical_file_id = 400001;
    session_entry.file_id = 401;
    session_entry.table_id = 4;
    session_entry.group_id = 4;
    session_entry.version_number = 1;
    session_entry.chunk_size = 4096;
    session_entry.replication_factor = 1;
    session_entry.expected_chunks = 2;
    session_entry.expires_at = 1000000000ULL;
    session_entry.encryption = static_cast<uint8_t>(EncryptionAlgo::NONE);
    session_entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    index.apply_session_open(session_entry);

    VersionCompleteEntry complete_entry;
    complete_entry.file_id = 401;
    complete_entry.logical_file_id = 400001;
    complete_entry.version_number = 1;
    complete_entry.content_checksum = 99999;
    complete_entry.total_size = 8192;
    complete_entry.chunk_count = 2;

    index.apply_version_complete(complete_entry);

    const VersionEntry* ver = index.get_version(400001, 1);
    ASSERT_NE(ver, nullptr);
    EXPECT_EQ(ver->state, VersionState::COMPLETE);
    EXPECT_EQ(ver->content_checksum, 99999);
    EXPECT_EQ(ver->total_size, 8192);

    const VersionEntry* latest = index.get_latest_complete(400001);
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->version_number, 1);
}

// Test: Get latest complete version
TEST_F(FileIndexTest, GetLatestCompleteVersion) {
    SessionOpenEntry session_entry;
    session_entry.logical_file_id = 500001;
    session_entry.file_id = 501;
    session_entry.table_id = 5;
    session_entry.group_id = 5;
    session_entry.chunk_size = 4096;
    session_entry.replication_factor = 1;
    session_entry.expected_chunks = 1;
    session_entry.expires_at = 1000000000ULL;
    session_entry.encryption = static_cast<uint8_t>(EncryptionAlgo::NONE);
    session_entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    // Create version 1
    session_entry.session_id = 5001;
    session_entry.version_number = 1;
    index.apply_session_open(session_entry);

    // Create version 2
    session_entry.session_id = 5002;
    session_entry.version_number = 2;
    index.apply_session_open(session_entry);

    // Complete version 2
    VersionCompleteEntry complete_entry;
    complete_entry.logical_file_id = 500001;
    complete_entry.file_id = 501;
    complete_entry.version_number = 2;
    complete_entry.content_checksum = 77777;
    complete_entry.total_size = 4096;
    complete_entry.chunk_count = 1;
    index.apply_version_complete(complete_entry);

    const VersionEntry* latest = index.get_latest_complete(500001);
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->version_number, 2);
}

// Test: Is chunk confirmed
TEST_F(FileIndexTest, IsChunkConfirmed) {
    SessionOpenEntry session_entry;
    session_entry.session_id = 6001;
    session_entry.logical_file_id = 600001;
    session_entry.file_id = 601;
    session_entry.table_id = 6;
    session_entry.group_id = 6;
    session_entry.version_number = 1;
    session_entry.chunk_size = 4096;
    session_entry.replication_factor = 1;
    session_entry.expected_chunks = 3;
    session_entry.expires_at = 1000000000ULL;
    session_entry.encryption = static_cast<uint8_t>(EncryptionAlgo::NONE);
    session_entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    index.apply_session_open(session_entry);

    ChunkConfirmedEntry chunk_entry;
    chunk_entry.session_id = 6001;
    chunk_entry.file_id = 601;
    chunk_entry.chunk_index = 0;
    chunk_entry.chunk_size_actual = 4096;
    chunk_entry.chunk_checksum = 11111;
    index.apply_chunk_confirmed(chunk_entry);

    EXPECT_TRUE(index.is_chunk_confirmed(6001, 0));
    EXPECT_FALSE(index.is_chunk_confirmed(6001, 1));
    EXPECT_FALSE(index.is_chunk_confirmed(9999, 0));  // Unknown session
}

// Test: Get confirmed chunks
TEST_F(FileIndexTest, GetConfirmedChunks) {
    SessionOpenEntry session_entry;
    session_entry.session_id = 7001;
    session_entry.logical_file_id = 700001;
    session_entry.file_id = 701;
    session_entry.table_id = 7;
    session_entry.group_id = 7;
    session_entry.version_number = 1;
    session_entry.chunk_size = 4096;
    session_entry.replication_factor = 1;
    session_entry.expected_chunks = 4;
    session_entry.expires_at = 1000000000ULL;
    session_entry.encryption = static_cast<uint8_t>(EncryptionAlgo::NONE);
    session_entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    index.apply_session_open(session_entry);

    for (int i = 0; i < 4; i += 2) {
        ChunkConfirmedEntry chunk_entry;
        chunk_entry.session_id = 7001;
        chunk_entry.file_id = 701;
        chunk_entry.chunk_index = i;
        chunk_entry.chunk_size_actual = 4096;
        chunk_entry.chunk_checksum = 30000 + i;
        index.apply_chunk_confirmed(chunk_entry);
    }

    auto confirmed = index.get_confirmed_chunks(7001);
    ASSERT_EQ(confirmed.size(), 2);
    EXPECT_EQ(confirmed[0], 0);
    EXPECT_EQ(confirmed[1], 2);
}

// Test: Version deleted
TEST_F(FileIndexTest, VersionDeleted) {
    SessionOpenEntry session_entry;
    session_entry.session_id = 8001;
    session_entry.logical_file_id = 800001;
    session_entry.file_id = 801;
    session_entry.table_id = 8;
    session_entry.group_id = 8;
    session_entry.version_number = 1;
    session_entry.chunk_size = 4096;
    session_entry.replication_factor = 1;
    session_entry.expected_chunks = 1;
    session_entry.expires_at = 1000000000ULL;
    session_entry.encryption = static_cast<uint8_t>(EncryptionAlgo::NONE);
    session_entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    index.apply_session_open(session_entry);

    VersionDeletedEntry deleted_entry;
    deleted_entry.logical_file_id = 800001;
    deleted_entry.file_id = 801;
    deleted_entry.version_number = 1;

    index.apply_version_deleted(deleted_entry);

    const VersionEntry* ver = index.get_version(800001, 1);
    ASSERT_NE(ver, nullptr);
    EXPECT_EQ(ver->state, VersionState::DELETED);
}

// Test: File deleted deletes all versions
TEST_F(FileIndexTest, FileDeleted) {
    SessionOpenEntry session_entry;
    session_entry.logical_file_id = 900001;
    session_entry.file_id = 901;
    session_entry.table_id = 9;
    session_entry.group_id = 9;
    session_entry.chunk_size = 4096;
    session_entry.replication_factor = 1;
    session_entry.expected_chunks = 1;
    session_entry.expires_at = 1000000000ULL;
    session_entry.encryption = static_cast<uint8_t>(EncryptionAlgo::NONE);
    session_entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    // Create two versions
    session_entry.session_id = 9001;
    session_entry.version_number = 1;
    index.apply_session_open(session_entry);

    session_entry.session_id = 9002;
    session_entry.version_number = 2;
    index.apply_session_open(session_entry);

    FileDeletedEntry file_deleted;
    file_deleted.logical_file_id = 900001;
    index.apply_file_deleted(file_deleted);

    const VersionEntry* ver1 = index.get_version(900001, 1);
    const VersionEntry* ver2 = index.get_version(900001, 2);
    
    ASSERT_NE(ver1, nullptr);
    ASSERT_NE(ver2, nullptr);
    EXPECT_EQ(ver1->state, VersionState::DELETED);
    EXPECT_EQ(ver2->state, VersionState::DELETED);
}

// Test: List files by table and group
TEST_F(FileIndexTest, ListFilesByTableAndGroup) {
    // Create files in different tables/groups
    SessionOpenEntry session_entry;
    session_entry.version_number = 1;
    session_entry.chunk_size = 4096;
    session_entry.replication_factor = 1;
    session_entry.expected_chunks = 1;
    session_entry.expires_at = 1000000000ULL;
    session_entry.encryption = static_cast<uint8_t>(EncryptionAlgo::NONE);
    session_entry.segment_type = static_cast<uint8_t>(SegmentType::STANDARD);

    session_entry.session_id = 10001;
    session_entry.logical_file_id = 1000001;
    session_entry.file_id = 1001;
    session_entry.table_id = 10;
    session_entry.group_id = 100;
    index.apply_session_open(session_entry);

    session_entry.session_id = 10002;
    session_entry.logical_file_id = 1000002;
    session_entry.file_id = 1002;
    session_entry.table_id = 10;
    session_entry.group_id = 100;
    index.apply_session_open(session_entry);

    session_entry.session_id = 10003;
    session_entry.logical_file_id = 1000003;
    session_entry.file_id = 1003;
    session_entry.table_id = 11;
    session_entry.group_id = 100;
    index.apply_session_open(session_entry);

    auto files_10_100 = index.list_files(10, 100);
    auto files_11_100 = index.list_files(11, 100);

    EXPECT_EQ(files_10_100.size(), 2);
    EXPECT_EQ(files_11_100.size(), 1);
}

// Test: ID generation
TEST_F(FileIndexTest, IDGeneration) {
    uint64_t id1 = index.next_logical_file_id();
    uint64_t id2 = index.next_logical_file_id();
    uint64_t id3 = index.next_logical_file_id();

    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);
    EXPECT_EQ(id3, 3);

    uint64_t file_id1 = index.next_file_id();
    uint64_t file_id2 = index.next_file_id();
    EXPECT_EQ(file_id1, 1);
    EXPECT_EQ(file_id2, 2);

    uint64_t session_id1 = index.next_session_id();
    uint64_t session_id2 = index.next_session_id();
    EXPECT_EQ(session_id1, 1);
    EXPECT_EQ(session_id2, 2);
}

}  // namespace filegroup
