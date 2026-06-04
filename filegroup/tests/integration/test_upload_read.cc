#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "config/config.h"
#include "engine/engine_service.h"

namespace filegroup {
namespace {

static std::string make_temp_dir() {
    char tmpl[] = "/tmp/filegroup_mv_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    return std::string(d) + "/";
}

class MaxVersionsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        data_dir_ = make_temp_dir();
        // Build minimal config
        config_.tls.ca_cert_file = "";
        config_.tls.engine_cert_file = "";
        config_.tls.engine_key_file = "";

        RegistryNodeConfig reg;
        reg.id = 1;
        reg.address = "";
        config_.registry_nodes.push_back(reg);

        StorageNodeConfig storage;
        storage.node_id = 1;
        storage.address = "";
        storage.role = NodeRole::ORIGIN;
        config_.storage_nodes.push_back(storage);

        FileGroupConfig group;
        group.group_id = 1;
        group.name = "test";
        group.chunk_size = 4096;
        group.replication_factor = 1;
        group.max_versions = 2;  // limit for testing
        group.file_expires_in_days = 0;
        config_.groups.push_back(group);

        FileTableConfig table;
        table.table_id = 1;
        table.name = "test";
        table.group_id = 1;
        table.max_versions = 2;
        table.file_expires_in_days = 0;
        config_.tables.push_back(table);

        server_ = std::make_unique<EngineServer>(config_, nullptr, data_dir_);
    }

    void TearDown() override {
        server_.reset();
        std::string cmd = "rm -rf " + data_dir_.substr(0, data_dir_.size() - 1);
        system(cmd.c_str());
    }

    // Helper: upload small data as a new version
    uint64_t upload_version(uint64_t logical_file_id, const std::vector<uint8_t>& data,
                            uint32_t table_id = 1, uint32_t group_id = 1) {
        auto s = server_->open_session(group_id, table_id, logical_file_id, data.size(), 1, 0);
        EXPECT_TRUE(s.success) << s.error;

        auto wr = server_->write_chunk(s.session_id, 0, data);
        EXPECT_TRUE(wr.success) << wr.error;

        auto cr = server_->complete_session(s.session_id, 0);
        EXPECT_TRUE(cr.success) << cr.error;

        return cr.logical_file_id;
    }

    ClusterConfig config_;
    std::unique_ptr<EngineServer> server_;
    std::string data_dir_;
};

// Upload 3 versions with max_versions=2 → MARKED_DELETED removed first
TEST_F(MaxVersionsIntegrationTest, MarkedDeletedRemovedBeforeComplete) {
    std::vector<uint8_t> data1 = {1, 2, 3, 4};
    std::vector<uint8_t> data2 = {5, 6, 7, 8};
    std::vector<uint8_t> data3 = {9, 0, 1, 2};

    // Upload v1 and v2 (both COMPLETE)
    uint64_t lid = upload_version(0, data1);  // v1
    upload_version(lid, data2);                // v2
    EXPECT_EQ(lid, 1u);

    // Verify v1 and v2 are both COMPLETE — use list_versions
    auto vers = server_->list_versions(lid);
    EXPECT_EQ(vers.size(), 2);
    for (auto& v : vers) {
        EXPECT_EQ(v.state, VersionState::COMPLETE);
    }

    // Delete v1 → MARKED_DELETED
    auto dr = server_->delete_version(lid, 1);
    EXPECT_TRUE(dr.success);

    // Verify v1 is now MARKED_DELETED
    vers = server_->list_versions(lid);
    bool found_v1 = false;
    for (auto& v : vers) {
        if (v.version_number == 1) {
            EXPECT_EQ(v.state, VersionState::MARKED_DELETED);
            found_v1 = true;
        }
    }
    EXPECT_TRUE(found_v1);

    // Upload v3 → should trigger enforcement: delete v1, keep v2 and v3
    upload_version(lid, data3);  // v3

    // Verify: v1 is DELETED, v2 and v3 are COMPLETE
    vers = server_->list_versions(lid);
    int complete_count = 0, deleted_count = 0;
    for (auto& v : vers) {
        if (v.version_number == 1) {
            EXPECT_EQ(v.state, VersionState::DELETED)
                << "MARKED_DELETED version should be reclaimed";
            deleted_count++;
        } else {
            EXPECT_EQ(v.state, VersionState::COMPLETE)
                << "Version " << v.version_number << " should be COMPLETE";
            complete_count++;
        }
    }
    EXPECT_EQ(complete_count, 2) << "Should have 2 COMPLETE versions";
}

// max_versions=1 → v1 deleted when v2 completes
TEST_F(MaxVersionsIntegrationTest, MaxOneKeepsOnlyNewest) {
    config_.groups[0].max_versions = 1;
    config_.tables[0].max_versions = 1;
    server_ = std::make_unique<EngineServer>(config_, nullptr, data_dir_);

    std::vector<uint8_t> data = {1, 2, 3};
    uint64_t lid = upload_version(0, data);  // v1
    upload_version(lid, data);                // v2 → enforcement deletes v1

    auto vers = server_->list_versions(lid);
    for (auto& v : vers) {
        if (v.version_number == 1)
            EXPECT_NE(v.state, VersionState::COMPLETE);
        else
            EXPECT_EQ(v.state, VersionState::COMPLETE);
    }
}

// Read file after auto-deletion — should still work
TEST_F(MaxVersionsIntegrationTest, ReadFileAfterAutoDelete) {
    std::vector<uint8_t> data1 = {0xAA, 0xBB, 0xCC, 0xDD};
    std::vector<uint8_t> data2 = {0x11, 0x22, 0x33, 0x44};

    uint64_t lid = upload_version(0, data1);  // v1
    upload_version(lid, data2);                // v2, v1 auto-deleted

    auto result = server_->read_file(lid, 0);
    EXPECT_FALSE(result.data.empty()) << result.error;
    EXPECT_EQ(result.data, data2);
}

}  // namespace
}  // namespace filegroup
