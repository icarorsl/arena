#include <gtest/gtest.h>

#include "config/config_resolver.h"
#include "common/types.h"

using namespace filegroup;

class ConfigResolverTest : public ::testing::Test {
protected:
    FileGroupConfig group;
    FileTableConfig table;
    uint64_t now_us = 1000000000000000ULL;  // Some reference timestamp

    void SetUp() override {
        // Initialize group with defaults
        group.group_id = 1;
        group.name = "default_group";
        group.chunk_size = 65536;
        group.min_chunk_bytes = 1024;
        group.replication_factor = 3;
        group.max_versions = 10;
        group.file_expires_in_days = 30;
        group.expiry_granularity = ExpiryGranularity::DAY;
        group.encryption = EncryptionAlgo::AES_256_GCM;
        group.encryption_key = EncryptionKeyConfig{"/keys/group.key"};

        // Initialize table with no overrides
        table.table_id = 101;
        table.name = "default_table";
        table.group_id = 1;
        table.chunk_size = 0;       // inherit
        table.replication_factor = 0;  // inherit
        table.max_versions = 0;     // inherit
        table.file_expires_in_days = 0;  // inherit
        table.expiry_granularity = ExpiryGranularity::UNSET;  // inherit
        table.encryption = EncryptionAlgo::NONE;  // inherit
    }
};

// ===== Chunk Size Tests =====
TEST_F(ConfigResolverTest, ChunkSizeFileOverride) {
    uint64_t size = resolve_chunk_size(group, &table, 131072);
    EXPECT_EQ(size, 131072u);
}

TEST_F(ConfigResolverTest, ChunkSizeTableOverride) {
    table.chunk_size = 262144;
    uint64_t size = resolve_chunk_size(group, &table, 0);
    EXPECT_EQ(size, 262144u);
}

TEST_F(ConfigResolverTest, ChunkSizeGroupDefault) {
    uint64_t size = resolve_chunk_size(group, &table, 0);
    EXPECT_EQ(size, 65536u);
}

TEST_F(ConfigResolverTest, ChunkSizeNoTableUsesGroup) {
    uint64_t size = resolve_chunk_size(group, nullptr, 0);
    EXPECT_EQ(size, 65536u);
}

TEST_F(ConfigResolverTest, ChunkSizeFilePrecedence) {
    table.chunk_size = 262144;
    uint64_t size = resolve_chunk_size(group, &table, 131072);
    EXPECT_EQ(size, 131072u);
}

// ===== Replication Factor Tests =====
TEST_F(ConfigResolverTest, ReplicationFactorFileOverride) {
    uint8_t rf = resolve_replication_factor(group, &table, 5);
    EXPECT_EQ(rf, 5);
}

TEST_F(ConfigResolverTest, ReplicationFactorTableOverride) {
    table.replication_factor = 2;
    uint8_t rf = resolve_replication_factor(group, &table, 0);
    EXPECT_EQ(rf, 2);
}

TEST_F(ConfigResolverTest, ReplicationFactorGroupDefault) {
    uint8_t rf = resolve_replication_factor(group, &table, 0);
    EXPECT_EQ(rf, 3);
}

// ===== Expiry Granularity Tests =====
TEST_F(ConfigResolverTest, GranularityFileOverride) {
    auto g = resolve_expiry_granularity(group, &table, ExpiryGranularity::WEEK);
    EXPECT_EQ(g, ExpiryGranularity::WEEK);
}

TEST_F(ConfigResolverTest, GranularityTableOverride) {
    table.expiry_granularity = ExpiryGranularity::MONTH;
    auto g = resolve_expiry_granularity(group, &table, ExpiryGranularity::UNSET);
    EXPECT_EQ(g, ExpiryGranularity::MONTH);
}

TEST_F(ConfigResolverTest, GranularityGroupDefault) {
    auto g = resolve_expiry_granularity(group, &table, ExpiryGranularity::UNSET);
    EXPECT_EQ(g, ExpiryGranularity::DAY);
}

// ===== Encryption Tests =====
TEST_F(ConfigResolverTest, EncryptionFileOverride) {
    auto enc = resolve_encryption(group, &table, EncryptionAlgo::NONE);
    EXPECT_EQ(enc, EncryptionAlgo::AES_256_GCM);  // Group default since file is NONE
}

TEST_F(ConfigResolverTest, EncryptionFileOverrideExplicit) {
    auto enc = resolve_encryption(group, &table, EncryptionAlgo::AES_256_GCM);
    EXPECT_EQ(enc, EncryptionAlgo::AES_256_GCM);
}

TEST_F(ConfigResolverTest, EncryptionTableOverridesGroup) {
    // When table explicitly sets to AES_256_GCM (overriding group's NONE), it should win
    table.encryption = EncryptionAlgo::AES_256_GCM;
    group.encryption = EncryptionAlgo::NONE;
    auto enc = resolve_encryption(group, &table, EncryptionAlgo::NONE);
    EXPECT_EQ(enc, EncryptionAlgo::AES_256_GCM);  // Table override of group
}

TEST_F(ConfigResolverTest, EncryptionGroupDefault) {
    table.encryption = EncryptionAlgo::NONE;
    auto enc = resolve_encryption(group, &table, EncryptionAlgo::NONE);
    EXPECT_EQ(enc, EncryptionAlgo::AES_256_GCM);
}

// ===== Encryption Key File Tests =====
TEST_F(ConfigResolverTest, EncryptionKeyFileTableOverride) {
    table.encryption_key = EncryptionKeyConfig{"/keys/table.key"};
    std::string key_file = resolve_encryption_key_file(group, &table);
    EXPECT_EQ(key_file, "/keys/table.key");
}

TEST_F(ConfigResolverTest, EncryptionKeyFileGroupDefault) {
    std::string key_file = resolve_encryption_key_file(group, &table);
    EXPECT_EQ(key_file, "/keys/group.key");
}

TEST_F(ConfigResolverTest, EncryptionKeyFileNoConfig) {
    group.encryption_key.reset();
    table.encryption_key.reset();
    std::string key_file = resolve_encryption_key_file(group, &table);
    EXPECT_EQ(key_file, "");
}

// ===== Max Versions Tests =====
TEST_F(ConfigResolverTest, MaxVersionsTableOverride) {
    table.max_versions = 20;
    uint32_t max_v = resolve_max_versions(group, &table);
    EXPECT_EQ(max_v, 20u);
}

TEST_F(ConfigResolverTest, MaxVersionsGroupDefault) {
    uint32_t max_v = resolve_max_versions(group, &table);
    EXPECT_EQ(max_v, 10u);
}

TEST_F(ConfigResolverTest, MaxVersionsNoLimit) {
    group.max_versions = 0;
    table.max_versions = 0;
    uint32_t max_v = resolve_max_versions(group, &table);
    EXPECT_EQ(max_v, 0u);
}

// ===== Expiry Timestamp Tests =====
TEST_F(ConfigResolverTest, ExpiresAtFileOverride) {
    uint64_t expires = resolve_expires_at(group, &table, 60, now_us);
    // Should be now + 60 days
    uint64_t expected = now_us + (60ULL * 24 * 60 * 60 * 1000000ULL);
    EXPECT_EQ(expires, expected);
}

TEST_F(ConfigResolverTest, ExpiresAtTableDefault) {
    table.file_expires_in_days = 45;
    uint64_t expires = resolve_expires_at(group, &table, 0, now_us);
    uint64_t expected = now_us + (45ULL * 24 * 60 * 60 * 1000000ULL);
    EXPECT_EQ(expires, expected);
}

TEST_F(ConfigResolverTest, ExpiresAtGroupDefault) {
    uint64_t expires = resolve_expires_at(group, &table, 0, now_us);
    // Group default is 30 days
    uint64_t expected = now_us + (30ULL * 24 * 60 * 60 * 1000000ULL);
    EXPECT_EQ(expires, expected);
}

TEST_F(ConfigResolverTest, ExpiresAtNoExpiry) {
    group.file_expires_in_days = 0;
    table.file_expires_in_days = 0;
    uint64_t expires = resolve_expires_at(group, &table, 0, now_us);
    EXPECT_EQ(expires, 0u);
}

TEST_F(ConfigResolverTest, ExpiresAtFilePrecedence) {
    table.file_expires_in_days = 45;
    group.file_expires_in_days = 30;
    // File override is 60
    uint64_t expires = resolve_expires_at(group, &table, 60, now_us);
    uint64_t expected = now_us + (60ULL * 24 * 60 * 60 * 1000000ULL);
    EXPECT_EQ(expires, expected);
}

// ===== Edge Cases =====
TEST_F(ConfigResolverTest, ResolveWithNullptrTable) {
    uint64_t chunk = resolve_chunk_size(group, nullptr, 0);
    EXPECT_EQ(chunk, 65536u);

    uint8_t rf = resolve_replication_factor(group, nullptr, 0);
    EXPECT_EQ(rf, 3);

    auto enc = resolve_encryption(group, nullptr, EncryptionAlgo::NONE);
    EXPECT_EQ(enc, EncryptionAlgo::AES_256_GCM);

    uint32_t max_v = resolve_max_versions(group, nullptr);
    EXPECT_EQ(max_v, 10u);
}

TEST_F(ConfigResolverTest, AllZeroDefaultsToGroupDefaults) {
    // Create minimal group and table with zeros
    FileGroupConfig minimal_group;
    minimal_group.chunk_size = 100;
    minimal_group.replication_factor = 2;
    minimal_group.max_versions = 5;

    FileTableConfig minimal_table;
    minimal_table.chunk_size = 0;
    minimal_table.replication_factor = 0;
    minimal_table.max_versions = 0;

    EXPECT_EQ(resolve_chunk_size(minimal_group, &minimal_table, 0), 100u);
    EXPECT_EQ(resolve_replication_factor(minimal_group, &minimal_table, 0), 2);
    EXPECT_EQ(resolve_max_versions(minimal_group, &minimal_table), 5u);
}
