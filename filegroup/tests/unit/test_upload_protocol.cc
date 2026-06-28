#include <gtest/gtest.h>
#include "upload/upload_protocol.h"
#include "storage/storage_node_service.h"

using namespace filegroup;

class UploadProtocolTest : public ::testing::Test {
protected:
    UploadProtocol upload_;
    
    void SetUp() override {
        // Reset storage nodes before each test
        StorageNodeService::reset_all_nodes();
    }
};

TEST_F(UploadProtocolTest, CreateSession) {
    SessionConfig config;
    config.replication_factor = 3;
    config.chunk_size_bytes = 1024;
    config.passphrase = "test_pass";
    
    auto session = upload_.create_session("test.txt", 10240, config);
    
    EXPECT_GT(session.session_id, 0);
    EXPECT_EQ(session.filename, "test.txt");
    EXPECT_EQ(session.file_size_bytes, 10240);
    EXPECT_EQ(session.total_chunks, 10);
    EXPECT_EQ(session.uploaded_chunks, 0);
    EXPECT_EQ(session.status, "open");
}

TEST_F(UploadProtocolTest, CreateSessionEmptyFilename) {
    SessionConfig config;
    
    EXPECT_THROW(
        upload_.create_session("", 1024, config),
        std::invalid_argument
    );
}

TEST_F(UploadProtocolTest, CreateSessionZeroSize) {
    SessionConfig config;
    
    EXPECT_THROW(
        upload_.create_session("test.txt", 0, config),
        std::invalid_argument
    );
}

TEST_F(UploadProtocolTest, CreateSessionZeroReplication) {
    SessionConfig config;
    config.replication_factor = 0;
    
    EXPECT_THROW(
        upload_.create_session("test.txt", 1024, config),
        std::invalid_argument
    );
}

TEST_F(UploadProtocolTest, ChunkCalculation) {
    SessionConfig config;
    config.chunk_size_bytes = 1000;
    
    // 1500 bytes with 1000-byte chunks = 2 chunks
    auto session = upload_.create_session("test.txt", 1500, config);
    EXPECT_EQ(session.total_chunks, 2);
    
    // Exact fit
    auto session2 = upload_.create_session("test2.txt", 2000, config);
    EXPECT_EQ(session2.total_chunks, 2);
    
    // Partial chunk
    auto session3 = upload_.create_session("test3.txt", 1001, config);
    EXPECT_EQ(session3.total_chunks, 2);
}

TEST_F(UploadProtocolTest, HasSession) {
    SessionConfig config;
    auto session = upload_.create_session("test.txt", 1024, config);
    
    EXPECT_TRUE(upload_.has_session(session.session_id));
    EXPECT_FALSE(upload_.has_session(999999));
}

TEST_F(UploadProtocolTest, GetSession) {
    SessionConfig config;
    auto created = upload_.create_session("test.txt", 1024, config);
    
    auto retrieved = upload_.get_session(created.session_id);
    
    EXPECT_EQ(retrieved.session_id, created.session_id);
    EXPECT_EQ(retrieved.filename, "test.txt");
    EXPECT_EQ(retrieved.file_size_bytes, 1024);
}

TEST_F(UploadProtocolTest, GetNonExistentSession) {
    EXPECT_THROW(
        upload_.get_session(999999),
        std::invalid_argument
    );
}

TEST_F(UploadProtocolTest, UploadChunk) {
    SessionConfig config;
    config.chunk_size_bytes = 1024;
    auto session = upload_.create_session("test_chunk.txt", 2048, config);
    // 2048 / 1024 = 2 chunks total
    
    ChunkUploadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 0;  // Valid index
    req.chunk_data = "chunk_data";
    req.is_final = false;
    
    auto resp = upload_.upload_chunk(req);
    
    EXPECT_TRUE(resp.success) << "Upload failed: " << resp.error;
    EXPECT_EQ(resp.chunk_index, 0);
    EXPECT_GE(resp.assigned_nodes.size(), 1);
}

TEST_F(UploadProtocolTest, UploadChunkNonExistentSession) {
    ChunkUploadRequest req;
    req.session_id = 999999;
    req.chunk_index = 0;
    req.chunk_data = "data";
    
    auto resp = upload_.upload_chunk(req);
    EXPECT_FALSE(resp.success);
}

TEST_F(UploadProtocolTest, UploadChunkOutOfRange) {
    SessionConfig config;
    config.chunk_size_bytes = 1024;
    auto session = upload_.create_session("test.txt", 2048, config);
    
    ChunkUploadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 100;  // Out of range
    req.chunk_data = "data";
    
    auto resp = upload_.upload_chunk(req);
    EXPECT_FALSE(resp.success);
}

TEST_F(UploadProtocolTest, UploadMultipleChunks) {
    SessionConfig config;
    config.chunk_size_bytes = 1024;
    auto session = upload_.create_session("test_multi.txt", 3072, config);  // 3 chunks
    
    for (uint32_t i = 0; i < 3; ++i) {
        ChunkUploadRequest req;
        req.session_id = session.session_id;
        req.chunk_index = i;  // Use valid indices 0, 1, 2
        req.chunk_data = "chunk_" + std::to_string(i);
        req.is_final = (i == 2);
        
        auto resp = upload_.upload_chunk(req);
        EXPECT_TRUE(resp.success) << "Failed to upload chunk " << i << ": " << resp.error;
    }
    
    auto final_session = upload_.get_session(session.session_id);
    EXPECT_EQ(final_session.uploaded_chunks, 3);
}

TEST_F(UploadProtocolTest, FinalizeSession) {
    SessionConfig config;
    config.chunk_size_bytes = 1024;
    auto session = upload_.create_session("test_finalize.txt", 1024, config);
    // 1 chunk total
    
    // Upload the only chunk
    ChunkUploadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 0;  // Valid index
    req.chunk_data = "finalize_data";
    req.is_final = true;
    
    auto resp = upload_.upload_chunk(req);
    EXPECT_TRUE(resp.success) << "Upload failed: " << resp.error;
    
    bool finalized = upload_.finalize_session(session.session_id);
    EXPECT_TRUE(finalized);
    
    auto final = upload_.get_session(session.session_id);
    EXPECT_EQ(final.status, "complete");
}


TEST_F(UploadProtocolTest, FinalizeIncompleteSession) {
    SessionConfig config;
    config.chunk_size_bytes = 1024;
    auto session = upload_.create_session("test.txt", 2048, config);  // 2 chunks
    
    // Only upload 1 chunk
    ChunkUploadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 0;
    req.chunk_data = "data";
    
    upload_.upload_chunk(req);
    
    bool finalized = upload_.finalize_session(session.session_id);
    EXPECT_FALSE(finalized);  // Can't finalize, missing chunks
}

TEST_F(UploadProtocolTest, CancelSession) {
    SessionConfig config;
    auto session = upload_.create_session("test.txt", 1024, config);
    
    bool cancelled = upload_.cancel_session(session.session_id);
    EXPECT_TRUE(cancelled);
    
    auto final = upload_.get_session(session.session_id);
    EXPECT_EQ(final.status, "cancelled");
}

TEST_F(UploadProtocolTest, CancelNonExistentSession) {
    bool cancelled = upload_.cancel_session(999999);
    EXPECT_FALSE(cancelled);
}

TEST_F(UploadProtocolTest, MultipleSessionsIndependent) {
    SessionConfig config;
    auto session1 = upload_.create_session("file1.txt", 1024, config);
    auto session2 = upload_.create_session("file2.txt", 2048, config);
    
    EXPECT_NE(session1.session_id, session2.session_id);
    EXPECT_EQ(session1.filename, "file1.txt");
    EXPECT_EQ(session2.filename, "file2.txt");
}

TEST_F(UploadProtocolTest, UploadAssignsNodes) {
    SessionConfig config;
    auto session = upload_.create_session("test_nodes.txt", 1024, config);  // 1 chunk
    
    ChunkUploadRequest req;
    req.session_id = session.session_id;
    req.chunk_index = 0;  // Valid index
    req.chunk_data = "test_data";
    
    auto resp = upload_.upload_chunk(req);
    
    EXPECT_TRUE(resp.success) << "Upload failed: " << resp.error;
    EXPECT_EQ(resp.chunk_index, 0);
    EXPECT_GT(resp.assigned_nodes.size(), 0);
    
    // All assigned nodes should be valid
    for (uint32_t node_id : resp.assigned_nodes) {
        EXPECT_GT(node_id, 0);
    }
}

TEST_F(UploadProtocolTest, LargeFile) {
    SessionConfig config;
    config.chunk_size_bytes = 1024 * 1024;  // 1 MB chunks
    
    // 100 MB file = 100 chunks
    auto session = upload_.create_session("large.bin", 100 * 1024 * 1024, config);
    
    EXPECT_EQ(session.total_chunks, 100);
    EXPECT_EQ(session.uploaded_chunks, 0);
}

TEST_F(UploadProtocolTest, SessionIdUniqueness) {
    SessionConfig config;
    auto session1 = upload_.create_session("file1.txt", 1024, config);
    auto session2 = upload_.create_session("file2.txt", 1024, config);
    auto session3 = upload_.create_session("file3.txt", 1024, config);
    
    EXPECT_NE(session1.session_id, session2.session_id);
    EXPECT_NE(session2.session_id, session3.session_id);
    EXPECT_NE(session1.session_id, session3.session_id);
}
