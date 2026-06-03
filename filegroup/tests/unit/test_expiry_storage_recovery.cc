#include <gtest/gtest.h>
#include "expiry/expiry_service.h"
#include "recovery/storage_recovery.h"
#include "common/clock.h"

using namespace filegroup;

// Expiry Service Tests
class ExpiryServiceTest : public ::testing::Test {};

TEST_F(ExpiryServiceTest, RunExpiryCycle) {
    ExpiryService es;
    
    auto stats = es.run_expiry_cycle();
    
    EXPECT_GE(stats.last_scan_us, 0);
}

TEST_F(ExpiryServiceTest, IsExpired) {
    ExpiryService es;
    
    uint64_t now = now_us();
    
    bool past_expired = es.is_expired(now - 1000);
    EXPECT_TRUE(past_expired);
    
    bool future_expired = es.is_expired(now + 1000);
    EXPECT_FALSE(future_expired);
}

TEST_F(ExpiryServiceTest, GetStats) {
    ExpiryService es;
    
    es.run_expiry_cycle();
    auto stats = es.get_stats();
    
    EXPECT_EQ(stats.versions_expired, 0);
    EXPECT_EQ(stats.chunks_deleted, 0);
}

TEST_F(ExpiryServiceTest, SetScanInterval) {
    ExpiryService es;
    
    es.set_scan_interval_us(120000000);  // 2 minutes
    
    auto stats = es.get_stats();
    EXPECT_GE(stats.last_scan_us, 0);
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
