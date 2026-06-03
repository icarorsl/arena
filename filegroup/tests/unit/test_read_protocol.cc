#include <gtest/gtest.h>
#include "read/read_protocol.h"
#include "storage/storage_node_service.h"
#include "common/crc32c.h"

using namespace filegroup;

class ReadProtocolTest : public ::testing::Test {
protected:
    ReadProtocol read_;
    
    void SetUp() override {
        StorageNodeService::reset_all_nodes();
        
        // Pre-populate some chunks in storage nodes
        for (int node = 1; node <= 3; ++node) {
            auto service = StorageNodeService::get_node_service(node);
            
            for (int chunk = 0; chunk < 5; ++chunk) {
                UploadChunkRequest req;
                req.chunk_index = chunk;
                req.chunk_data = "chunk_" + std::to_string(node) + "_" + std::to_string(chunk);
                req.chunk_checksum = crc32c(
                    reinterpret_cast<const uint8_t*>(req.chunk_data.data()),
                    req.chunk_data.size()
                );
                req.replication_factor = 3;
                
                service->upload_chunk(req);
            }
        }
    }
};

TEST_F(ReadProtocolTest, OpenSession) {
    ReadSessionConfig config;
    config.file_id = 1;
    config.version_id = 0;
    
    auto session = read_.open_session(config);
    
    EXPECT_GT(session.session_id, 0);
    EXPECT_EQ(session.file_id, 1);
    EXPECT_EQ(session.status, "open");
    EXPECT_EQ(session.chunks_read, 0);
}

TEST_F(ReadProtocolTest, OpenSessionZeroFileId) {
    ReadSessionConfig config;
    config.file_id = 0;
    
    EXPECT_THROW(
        read_.open_session(config),
        std::invalid_argument
    );
}

TEST_F(ReadProtocolTest, HasSession) {
    ReadSessionConfig config;
    config.file_id = 1;
    
    auto session = read_.open_session(config);
    
    EXPECT_TRUE(read_.has_session(session.session_id));
    EXPECT_FALSE(read_.has_session(999));
}

TEST_F(ReadProtocolTest, GetSession) {
    ReadSessionConfig config;
    config.file_id = 1;
    config.offset_bytes = 100;
    
    auto session1 = read_.open_session(config);
    auto session2 = read_.get_session(session1.session_id);
    
    EXPECT_EQ(session2.session_id, session1.session_id);
    EXPECT_EQ(session2.file_id, 1);
    EXPECT_EQ(session2.offset_bytes, 100);
    EXPECT_EQ(session2.status, "open");
}

TEST_F(ReadProtocolTest, ReadChunk) {
    ReadSessionConfig config;
    config.file_id = 1;
    
    auto session = read_.open_session(config);
    
    ChunkReadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 0;
    req.verify_checksum = false;
    
    auto resp = read_.read_chunk(req);
    
    EXPECT_TRUE(resp.success) << "Read failed: " << resp.error;
    EXPECT_EQ(resp.chunk_index, 0);
    EXPECT_GT(resp.chunk_data.size(), 0);
    EXPECT_GT(resp.successful_node_id, 0);
}

TEST_F(ReadProtocolTest, ReadChunkNonExistentSession) {
    ChunkReadRequest req;
    req.session_id = 999;
    req.chunk_index = 0;
    
    auto resp = read_.read_chunk(req);
    
    EXPECT_FALSE(resp.success);
    EXPECT_EQ(resp.error, "session not found");
}

TEST_F(ReadProtocolTest, ReadChunkVerifyChecksum) {
    ReadSessionConfig config;
    config.file_id = 1;
    
    auto session = read_.open_session(config);
    
    ChunkReadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 0;
    req.verify_checksum = true;
    
    auto resp = read_.read_chunk(req);
    
    EXPECT_TRUE(resp.success) << "Read failed: " << resp.error;
    EXPECT_EQ(resp.chunk_index, 0);
    EXPECT_GT(resp.chunk_data.size(), 0);
    
    // Verify the checksum matches
    uint32_t computed = crc32c(
        reinterpret_cast<const uint8_t*>(resp.chunk_data.data()),
        resp.chunk_data.size()
    );
    EXPECT_EQ(computed, resp.chunk_checksum);
}

TEST_F(ReadProtocolTest, ReadMultipleChunks) {
    ReadSessionConfig config;
    config.file_id = 1;
    
    auto session = read_.open_session(config);
    
    for (int i = 0; i < 3; ++i) {
        ChunkReadRequest req;
        req.session_id = session.session_id;
        req.chunk_index = i;
        req.verify_checksum = true;
        
        auto resp = read_.read_chunk(req);
        EXPECT_TRUE(resp.success) << "Failed to read chunk " << i << ": " << resp.error;
    }
    
    auto updated = read_.get_session(session.session_id);
    EXPECT_EQ(updated.chunks_read, 3);
}

TEST_F(ReadProtocolTest, ReadFromMultipleReplicas) {
    ReadSessionConfig config;
    config.file_id = 1;
    
    auto session = read_.open_session(config);
    
    ChunkReadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 2;
    req.verify_checksum = false;
    
    auto resp = read_.read_chunk(req);
    
    EXPECT_TRUE(resp.success);
    EXPECT_GT(resp.node_ids_tried.size(), 0);
    EXPECT_GT(resp.successful_node_id, 0);
}

TEST_F(ReadProtocolTest, CompleteSession) {
    ReadSessionConfig config;
    config.file_id = 1;
    
    auto session = read_.open_session(config);
    
    ChunkReadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 0;
    
    read_.read_chunk(req);
    
    bool completed = read_.complete_session(session.session_id);
    EXPECT_TRUE(completed);
    
    auto updated = read_.get_session(session.session_id);
    EXPECT_EQ(updated.status, "complete");
}

TEST_F(ReadProtocolTest, CancelSession) {
    ReadSessionConfig config;
    config.file_id = 1;
    
    auto session = read_.open_session(config);
    
    bool cancelled = read_.cancel_session(session.session_id);
    EXPECT_TRUE(cancelled);
    
    auto updated = read_.get_session(session.session_id);
    EXPECT_EQ(updated.status, "cancelled");
}

TEST_F(ReadProtocolTest, CancelNonExistentSession) {
    bool cancelled = read_.cancel_session(999);
    EXPECT_FALSE(cancelled);
}

TEST_F(ReadProtocolTest, SessionProgress) {
    ReadSessionConfig config;
    config.file_id = 1;
    
    auto session = read_.open_session(config);
    EXPECT_EQ(session.chunks_read, 0);
    EXPECT_EQ(session.bytes_read, 0);
    
    ChunkReadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 0;
    
    read_.read_chunk(req);
    
    auto updated = read_.get_session(session.session_id);
    EXPECT_EQ(updated.chunks_read, 1);
    EXPECT_GT(updated.bytes_read, 0);
}

TEST_F(ReadProtocolTest, MultipleSessionsIndependent) {
    ReadSessionConfig config1, config2;
    config1.file_id = 1;
    config2.file_id = 2;
    
    auto s1 = read_.open_session(config1);
    auto s2 = read_.open_session(config2);
    
    EXPECT_NE(s1.session_id, s2.session_id);
    EXPECT_EQ(s1.file_id, 1);
    EXPECT_EQ(s2.file_id, 2);
    
    // Read from first session
    ChunkReadRequest req1;
    req1.session_id = s1.session_id;
    req1.chunk_index = 0;
    read_.read_chunk(req1);
    
    // Second session should be unchanged
    auto s2_check = read_.get_session(s2.session_id);
    EXPECT_EQ(s2_check.chunks_read, 0);
}

TEST_F(ReadProtocolTest, LargeChunkSize) {
    ReadSessionConfig config;
    config.file_id = 1;
    config.length_bytes = 1000000;  // 1MB
    
    auto session = read_.open_session(config);
    
    EXPECT_EQ(session.length_bytes, 1000000);
    EXPECT_EQ(session.status, "open");
}

TEST_F(ReadProtocolTest, SessionIdUniqueness) {
    ReadSessionConfig config;
    config.file_id = 1;
    
    std::set<uint32_t> ids;
    for (int i = 0; i < 10; ++i) {
        auto session = read_.open_session(config);
        ids.insert(session.session_id);
    }
    
    EXPECT_EQ(ids.size(), 10);  // All IDs should be unique
}
