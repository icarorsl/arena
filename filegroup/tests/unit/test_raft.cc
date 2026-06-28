#include <gtest/gtest.h>
#include <filesystem>

#include "raft/raft.h"

namespace filegroup {

class RaftTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/raft_test_" + std::to_string(rand());
        std::filesystem::create_directories(test_dir_);
        log_path_ = test_dir_ + "/raft.log";
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
    std::string log_path_;
};

// Test: Create Raft registry
TEST_F(RaftTest, CreateRegistry) {
    RaftRegistry raft(log_path_, 1, 1);
    
    const RaftState& state = raft.get_state();
    EXPECT_EQ(state.current_term, 1);
    EXPECT_EQ(state.voted_for, 0);
    EXPECT_EQ(state.commit_index, 0);
    EXPECT_EQ(state.last_applied_index, 0);
}

// Test: Append entry
TEST_F(RaftTest, AppendEntry) {
    RaftRegistry raft(log_path_, 1, 1);
    
    uint8_t data[16] = {0xAA, 0xBB, 0xCC};
    uint64_t index1 = raft.append_entry(data, 3);
    EXPECT_EQ(index1, 1);
    
    uint64_t index2 = raft.append_entry(data, 3);
    EXPECT_EQ(index2, 2);
    
    EXPECT_EQ(raft.next_log_index(), 3);
}

// Test: Commit entries
TEST_F(RaftTest, CommitEntries) {
    RaftRegistry raft(log_path_, 1, 1);
    
    uint8_t data[16] = {0xAA};
    raft.append_entry(data, 1);
    raft.append_entry(data, 1);
    raft.append_entry(data, 1);

    raft.commit_up_to(2);

    const RaftState& state = raft.get_state();
    EXPECT_EQ(state.commit_index, 2);
    EXPECT_EQ(state.last_applied_index, 2);
}

// Test: Commit beyond log size fails
TEST_F(RaftTest, CommitBeyondLogFails) {
    RaftRegistry raft(log_path_, 1, 1);
    
    uint8_t data[16] = {0xAA};
    raft.append_entry(data, 1);

    EXPECT_THROW(raft.commit_up_to(10), std::runtime_error);
}

// Test: Last log term
TEST_F(RaftTest, LastLogTerm) {
    RaftRegistry raft(log_path_, 1, 1);
    
    EXPECT_EQ(raft.last_log_term(), 0);  // Empty log
    
    uint8_t data[16] = {0xAA};
    raft.append_entry(data, 1);
    
    EXPECT_EQ(raft.last_log_term(), 1);  // Term 1
}

// Test: Next log index
TEST_F(RaftTest, NextLogIndex) {
    RaftRegistry raft(log_path_, 1, 1);
    
    EXPECT_EQ(raft.next_log_index(), 1);
    
    uint8_t data[16] = {0xAA};
    raft.append_entry(data, 1);
    EXPECT_EQ(raft.next_log_index(), 2);
    
    raft.append_entry(data, 1);
    EXPECT_EQ(raft.next_log_index(), 3);
}

// Test: Multiple entries with different data
TEST_F(RaftTest, MultipleEntriesWithData) {
    RaftRegistry raft(log_path_, 1, 1);
    
    for (int i = 0; i < 10; i++) {
        uint8_t data[16];
        std::memset(data, i, sizeof(data));
        uint64_t index = raft.append_entry(data, 16);
        EXPECT_EQ(index, i + 1);
    }

    EXPECT_EQ(raft.next_log_index(), 11);
}

// Test: RaftLogEntry size
TEST(RaftTypes, LogEntrySize) {
    EXPECT_EQ(sizeof(RaftLogEntry), 24);
}

}  // namespace filegroup
