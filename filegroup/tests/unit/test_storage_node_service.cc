#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

#include "storage/storage_node_service.h"
#include "common/crc32c.h"

namespace filegroup {

class StorageTest : public ::testing::Test {
protected:
    std::string dir_;
    void SetUp() override {
        // Use time + random for unique directory
        srand(time(nullptr) ^ getpid());
        dir_ = "/tmp/storage_test_" + std::to_string(rand());
        mkdir(dir_.c_str(), 0755);
    }
    void TearDown() override {
        // Cleanup handled by OS /tmp
    }
};

TEST_F(StorageTest, StoreAndFetchChunk) {
    StorageServer server(1, dir_);
    StorageClient client(&server);

    std::vector<uint8_t> data(256, 0xAA);
    uint32_t cs = crc32c(data.data(), data.size());

    auto result = client.store_chunk(1, 0, 100, 5, data.data(), data.size(), cs, false, 0, ExpiryGranularity::UNSET);
    ASSERT_TRUE(result.success) << "error: " << result.error;
    EXPECT_GT(result.offset, 0);

    auto fetch = client.fetch_chunk(result.segment_file, result.offset, data.size());
    EXPECT_TRUE(fetch.success);
    EXPECT_EQ(fetch.data, data);
}

TEST_F(StorageTest, DeleteChunk) {
    StorageServer server(1, dir_);

    std::vector<uint8_t> data(64, 0xBB);
    uint32_t cs = crc32c(data.data(), data.size());

    auto result = server.store_chunk(1, 0, 100, 5, data.data(), data.size(), cs, false, 0, ExpiryGranularity::UNSET);
    ASSERT_TRUE(result.success);

    EXPECT_TRUE(server.delete_chunk(1, 0));
    EXPECT_FALSE(server.delete_chunk(1, 0)); // Already deleted
}

TEST_F(StorageTest, Ping) {
    StorageServer server(1, dir_);
    EXPECT_TRUE(server.ping());
}

TEST_F(StorageTest, ReportSegments) {
    StorageServer server(1, dir_);

    std::vector<uint8_t> data(32, 0xCC);
    uint32_t cs = crc32c(data.data(), data.size());
    server.store_chunk(1, 0, 100, 5, data.data(), data.size(), cs, false, 0, ExpiryGranularity::UNSET);

    auto report = server.report_segments();
    EXPECT_EQ(report.size(), 1);
}

TEST_F(StorageTest, ClientPing) {
    StorageServer server(1, dir_);
    StorageClient client(&server);

    EXPECT_TRUE(client.ping());

    std::vector<uint8_t> data(100, 0xDD);
    uint32_t cs = crc32c(data.data(), data.size());
    auto result = client.store_chunk(2, 1, 200, 10, data.data(), data.size(), cs, false);
    EXPECT_TRUE(result.success);
}

}  // namespace filegroup
