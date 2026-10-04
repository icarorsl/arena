#include <gtest/gtest.h>
#include "expiry/expiry_service.h"
#include "recovery/storage_recovery.h"
#include "common/clock.h"

using namespace filegroup;

// Expiry Service Tests
class ExpiryServiceTest : public ::testing::Test {};

TEST_F(ExpiryServiceTest, InitialStatsAreZero) {
    ExpiryService es(nullptr, 0);
    EXPECT_EQ(es.versions_expired(), 0);
    EXPECT_EQ(es.last_scan_us(), 0);
}

TEST_F(ExpiryServiceTest, StopWithoutStartIsSafe) {
    ExpiryService es(nullptr, 0);
    es.stop();
    SUCCEED();
}

// Storage Recovery Tests
class StorageRecoveryServiceTest : public ::testing::Test {};

TEST_F(StorageRecoveryServiceTest, RecoverNode) {
    StorageRecoveryService srs;
    
    uint64_t tasks = srs.recover_node(1);
    
    EXPECT_EQ(tasks, 0);  // Phase 1: no actual tasks
}

TEST_F(StorageRecoveryServiceTest, GetTaskStatus) {
    StorageRecoveryService srs;
    
    auto status = srs.get_task_status(0);
    
    EXPECT_EQ(status.chunk_index, 0);
    EXPECT_EQ(status.status, "unknown");
}

TEST_F(StorageRecoveryServiceTest, GetStats) {
    StorageRecoveryService srs;
    
    auto stats = srs.get_stats();
    
    EXPECT_EQ(stats.chunks_to_recover, 0);
    EXPECT_EQ(stats.chunks_recovered, 0);
}

TEST_F(StorageRecoveryServiceTest, ProcessRecoveryTask) {
    StorageRecoveryService srs;
    
    bool processed = srs.process_recovery_task(0);
    
    EXPECT_FALSE(processed);  // No tasks in Phase 1
}
