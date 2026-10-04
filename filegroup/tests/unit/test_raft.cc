#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>
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
// Multi-voter cluster: one leader is elected and a proposed entry is
// replicated (log + commit index) to every node.
TEST_F(RaftTest, ThreeNodesElectLeaderAndReplicate) {
    std::vector<std::string> paths = {lp_ + "1", lp_ + "2", lp_ + "3"};
    for (auto& p : paths) std::remove(p.c_str());

    std::atomic<int> applied1{0}, applied2{0}, applied3{0};
    auto make = [&](uint32_t id, const std::string& path, std::atomic<int>& applied) {
        RaftConfig c;
        c.local_node_id = id;
        c.peer_node_ids = {1, 2, 3};
        c.log_path = path;
        return std::make_unique<RaftNode>(
            std::move(c), std::make_unique<InProcessRaftTransport>(),
            [&applied](uint32_t, const void*, uint16_t, uint64_t) { applied++; });
    };

    auto n1 = make(1, paths[0], applied1);
    auto n2 = make(2, paths[1], applied2);
    auto n3 = make(3, paths[2], applied3);

    std::vector<RaftNode*> nodes = {n1.get(), n2.get(), n3.get()};
    std::vector<uint32_t> ids = {1, 2, 3};
    for (size_t i = 0; i < nodes.size(); i++) {
        for (size_t j = 0; j < nodes.size(); j++) {
            if (i != j) nodes[i]->register_inprocess_peer(ids[j], nodes[j]);
        }
    }

    // Wait for exactly one leader to emerge.
    RaftNode* leader = nullptr;
    for (int i = 0; i < 300 && !leader; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        int leaders = 0;
        for (auto* n : nodes) if (n->is_leader()) leaders++;
        if (leaders == 1) {
            for (auto* n : nodes) if (n->is_leader()) leader = n;
        } else {
            leader = nullptr;
        }
    }
    ASSERT_NE(leader, nullptr) << "no single leader elected";

    uint32_t entry = 7;
    auto [ok, lsn] = leader->propose(1, &entry, sizeof(entry));
    ASSERT_TRUE(ok) << "leader propose failed";
    EXPECT_EQ(lsn, 1u);

    // Wait for every node to commit and apply the entry.
    for (int i = 0; i < 300; i++) {
        if (n1->commit_index() >= 1 && n2->commit_index() >= 1 && n3->commit_index() >= 1 &&
            applied1.load() >= 1 && applied2.load() >= 1 && applied3.load() >= 1) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(n1->commit_index(), 1u);
    EXPECT_EQ(n2->commit_index(), 1u);
    EXPECT_EQ(n3->commit_index(), 1u);
    EXPECT_EQ(n1->log_size(), 1u);
    EXPECT_EQ(n2->log_size(), 1u);
    EXPECT_EQ(n3->log_size(), 1u);
    EXPECT_EQ(applied1.load(), 1);
    EXPECT_EQ(applied2.load(), 1);
    EXPECT_EQ(applied3.load(), 1);
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
