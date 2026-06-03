#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstdio>
#include "raft/raft.h"

namespace filegroup {
class RaftTest : public ::testing::Test {
protected:
    std::string lp_;
    void SetUp() override { lp_="/tmp/rt_"+std::to_string(rand())+".log"; std::remove(lp_.c_str()); }
    void TearDown() override { std::remove(lp_.c_str()); }
};
TEST_F(RaftTest, SingleNodeLeader) {
    RaftConfig c; c.local_node_id=1; c.peer_node_ids={1}; c.log_path=lp_;
    auto t=std::make_unique<InProcessRaftTransport>();
    RaftNode n(std::move(c),std::move(t),[](uint32_t,const void*,uint16_t,uint64_t){});
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    EXPECT_TRUE(n.is_leader());
}
TEST_F(RaftTest, ProposeAndApply) {
    RaftConfig c; c.local_node_id=1; c.peer_node_ids={1}; c.log_path=lp_;
    uint32_t a=0; auto cb=[&](uint32_t t,const void*,uint16_t,uint64_t){a=t;};
    auto t=std::make_unique<InProcessRaftTransport>();
    RaftNode n(std::move(c),std::move(t),cb);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    ASSERT_TRUE(n.is_leader());
    uint32_t d=42; auto[ok,lsn]=n.propose(1,&d,sizeof(d));
    EXPECT_TRUE(ok); EXPECT_EQ(lsn,1u);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(a,1u);
}
TEST_F(RaftTest, LogGrows) {
    RaftConfig c; c.local_node_id=1; c.peer_node_ids={1}; c.log_path=lp_;
    auto t=std::make_unique<InProcessRaftTransport>();
    RaftNode n(std::move(c),std::move(t),[](uint32_t,const void*,uint16_t,uint64_t){});
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    ASSERT_TRUE(n.is_leader());
    uint32_t d=42; n.propose(1,&d,sizeof(d)); n.propose(2,&d,sizeof(d));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(n.log_size(),2u);
}
}  // namespace filegroup
