#include <gtest/gtest.h>
#include "versioning/versioning_service.h"
#include "heartbeat/heartbeat_service.h"
#include "recovery/engine_recovery.h"
#include "scrubber/scrubber_service.h"

using namespace filegroup;

// Versioning Tests
class VersioningServiceTest : public ::testing::Test {};

TEST_F(VersioningServiceTest, CreateVersion) {
    VersioningService vs;
    VersionConfig config;
    config.max_versions = 5;
    config.retention_days = 30;
    
    uint32_t version_id = vs.create_version(1, config);
    EXPECT_GT(version_id, 0);
}

TEST_F(VersioningServiceTest, CreateVersionZeroFileId) {
    VersioningService vs;
    VersionConfig config;
    
    EXPECT_THROW(vs.create_version(0, config), std::invalid_argument);
}

TEST_F(VersioningServiceTest, CompleteVersion) {
    VersioningService vs;
    VersionConfig config;
    
    uint32_t version_id = vs.create_version(1, config);
    bool completed = vs.complete_version(1, version_id);
    
    EXPECT_TRUE(completed);
    
    auto info = vs.get_version(1, version_id);
    EXPECT_EQ(info.state, "complete");
}

TEST_F(VersioningServiceTest, GetLatestComplete) {
    VersioningService vs;
    VersionConfig config;
    
    uint32_t v1 = vs.create_version(1, config);
    uint32_t v2 = vs.create_version(1, config);
    
    vs.complete_version(1, v1);
    vs.complete_version(1, v2);
    
    uint32_t latest = vs.get_latest_complete(1);
    EXPECT_NE(latest, 0);  // Should return a valid version ID
}

TEST_F(VersioningServiceTest, EnforceMaxVersions) {
    VersioningService vs;
    VersionConfig config;
    config.max_versions = 2;
    
    uint32_t v1 = vs.create_version(1, config);
    uint32_t v2 = vs.create_version(1, config);
    uint32_t v3 = vs.create_version(1, config);
    
    int deleted = vs.enforce_max_versions(1, 2);
    EXPECT_GT(deleted, 0);
}

TEST_F(VersioningServiceTest, ListVersions) {
    VersioningService vs;
    VersionConfig config;
    
    vs.create_version(1, config);
    vs.create_version(1, config);
    vs.create_version(1, config);
    
    auto versions = vs.list_versions(1);
    EXPECT_EQ(versions.size(), 3);
}

// Heartbeat Tests
class HeartbeatServiceTest : public ::testing::Test {};

TEST_F(HeartbeatServiceTest, UnknownNodeDefaultsToHealthy) {
    std::vector<StorageClient*> clients;
    HeartbeatService hs(clients, nullptr, 0);
    EXPECT_EQ(hs.get_state(1), NodeState::HEALTHY);
}

TEST_F(HeartbeatServiceTest, StopWithoutStartIsSafe) {
    std::vector<StorageClient*> clients;
    HeartbeatService hs(clients, nullptr, 0);
    hs.stop();
    SUCCEED();
}

// Recovery Tests
class EngineRecoveryTest : public ::testing::Test {};

TEST_F(EngineRecoveryTest, RecoverFromManifest) {
    EngineRecoveryService ers;
    
    auto stats = ers.recover_from_manifest("/tmp/manifest.log");
    
    EXPECT_GE(stats.recovery_time_us, 0);
}

TEST_F(EngineRecoveryTest, IsRecoveryNeeded) {
    EngineRecoveryService ers;
    
    bool needed = ers.is_recovery_needed("/tmp/manifest.log");
    EXPECT_TRUE(needed);
}

TEST_F(EngineRecoveryTest, GetLastRecoveryStats) {
    EngineRecoveryService ers;
    
    auto stats = ers.recover_from_manifest("/tmp/manifest.log");
    auto retrieved = ers.get_last_recovery_stats();
    
    EXPECT_EQ(retrieved.recovery_time_us, stats.recovery_time_us);
}

// Scrubbing Tests
class ScrubberServiceTest : public ::testing::Test {};

TEST_F(ScrubberServiceTest, StartScrub) {
    ScrubberService ss;
    
    ss.start_scrub();
    
    auto stats = ss.get_stats();
    EXPECT_EQ(stats.chunks_scanned, 0);
}

TEST_F(ScrubberServiceTest, VerifyChunk) {
    ScrubberService ss;
    
    ss.start_scrub();
    bool verified = ss.verify_chunk(1, 1, 0);
    
    EXPECT_TRUE(verified);
    
    auto stats = ss.get_stats();
    EXPECT_GT(stats.chunks_scanned, 0);
}

TEST_F(ScrubberServiceTest, CheckReplication) {
    ScrubberService ss;
    
    uint32_t replicas = ss.check_replication(0);
    EXPECT_EQ(replicas, 3);
}

TEST_F(ScrubberServiceTest, GetCorruptedChunks) {
    ScrubberService ss;
    
    auto corrupted = ss.get_corrupted_chunks();
    EXPECT_EQ(corrupted.size(), 0);
}
