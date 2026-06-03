#include <gtest/gtest.h>
#include <set>
#include "distribution/distribution.h"

namespace filegroup {

TEST(DistributionTest, BasicAssignment) {
    // 8 chunks, 3x replication, 4 nodes
    auto assignments = assign_chunks(42, 8, 3, {1, 2, 3, 4});

    EXPECT_EQ(assignments.size(), 8);
    for (size_t i = 0; i < assignments.size(); ++i) {
        EXPECT_EQ(assignments[i].chunk_index, i);
        EXPECT_TRUE(assignments[i].primary_node_id >= 1);
        EXPECT_TRUE(assignments[i].primary_node_id <= 4);
        // With 3 nodes and 3x replication, should have primary + 2 replicas
        EXPECT_EQ(assignments[i].replica_node_ids.size(), 2);
    }
}

TEST(DistributionTest, SingleNodeNoReplicas) {
    auto assignments = assign_chunks(1, 5, 1, {10});
    EXPECT_EQ(assignments.size(), 5);
    for (const auto& a : assignments) {
        EXPECT_EQ(a.primary_node_id, 10);
        EXPECT_TRUE(a.replica_node_ids.empty());
    }
}

TEST(DistributionTest, Deterministic) {
    auto a1 = assign_chunks(100, 20, 3, {1, 2, 3, 4, 5});
    auto a2 = assign_chunks(100, 20, 3, {1, 2, 3, 4, 5});
    for (size_t i = 0; i < a1.size(); ++i) {
        EXPECT_EQ(a1[i].primary_node_id, a2[i].primary_node_id);
        EXPECT_EQ(a1[i].replica_node_ids, a2[i].replica_node_ids);
    }
}

TEST(DistributionTest, DifferentFileIdsSpreadDifferently) {
    auto a1 = assign_chunks(1, 5, 2, {1, 2, 3, 4, 5});
    auto a2 = assign_chunks(2, 5, 2, {1, 2, 3, 4, 5});
    // Different file_ids should give different primary for chunk 0
    EXPECT_NE(a1[0].primary_node_id, a2[0].primary_node_id);
}

TEST(DistributionTest, NoDuplicatesWithinChunk) {
    auto assignments = assign_chunks(1, 50, 4, {1, 2, 3, 4, 5});
    for (const auto& a : assignments) {
        std::set<uint16_t> nodes = {a.primary_node_id};
        for (auto r : a.replica_node_ids) nodes.insert(r);
        EXPECT_EQ(nodes.size(), 1 + a.replica_node_ids.size());
    }
}

TEST(DistributionTest, RingWrapAround) {
    // 2 nodes, 3x replication: replicas will wrap around
    auto assignments = assign_chunks(1, 3, 3, {1, 2});
    EXPECT_EQ(assignments.size(), 3);
    // Each chunk: primary + replicas (may be fewer if not enough distinct nodes)
    for (const auto& a : assignments) {
        EXPECT_GE(a.replica_node_ids.size(), 1);
    }
}

}  // namespace filegroup
