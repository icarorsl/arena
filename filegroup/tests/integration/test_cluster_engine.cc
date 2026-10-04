#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "cluster/cluster_node.h"
#include "config/config.h"

namespace filegroup {

// End-to-end: a 3-node cluster (Raft over gRPC + chunk replication over gRPC)
// accepts an upload on the leader and serves the file back from a different
// node, proving metadata and chunk replication both work across the network.
TEST(ClusterEngine, UploadReplicatesAndReadsFromAnotherNode) {
    std::string base = "/tmp/cluster_engine_" + std::to_string(rand());
    std::filesystem::remove_all(base);

    ClusterConfig config;
    for (uint32_t id = 1; id <= 3; id++) {
        RegistryNodeConfig rn;
        rn.id = id;
        rn.address = "127.0.0.1:" + std::to_string(51160 + id);
        config.registry_nodes.push_back(rn);

        StorageNodeConfig sn;
        sn.node_id = id;
        sn.address = "127.0.0.1:" + std::to_string(51260 + id);
        sn.role = NodeRole::ORIGIN;
        config.storage_nodes.push_back(sn);
    }

    FileGroupConfig group;
    group.group_id = 1;
    group.name = "default";
    group.chunk_size = 65536;
    group.min_chunk_bytes = 1024;
    group.replication_factor = 3;
    group.max_versions = 5;
    group.file_expires_in_days = 0;
    group.expiry_granularity = ExpiryGranularity::UNSET;
    group.encryption = EncryptionAlgo::NONE;
    config.groups.push_back(group);

    std::vector<std::unique_ptr<ClusterNode>> nodes;
    for (uint32_t id = 1; id <= 3; id++) {
        nodes.push_back(std::make_unique<ClusterNode>(
            config, nullptr, base + "/n" + std::to_string(id), id));
    }

    // Wait for exactly one Raft leader.
    int leader = -1;
    for (int i = 0; i < 800 && leader < 0; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        int count = 0, candidate = -1;
        for (int k = 0; k < 3; k++) {
            if (nodes[k]->is_leader()) {
                count++;
                candidate = k;
            }
        }
        leader = (count == 1) ? candidate : -1;
    }
    ASSERT_GE(leader, 0) << "no leader elected in cluster";

    // Tables must exist before uploads (created via a Raft-replicated entry).
    auto table = nodes[leader]->server().create_table(1, 1, "default", 0, 5);
    ASSERT_TRUE(table.success) << table.error;

    std::string payload(3000, 'x');
    for (size_t i = 0; i < payload.size(); i++) payload[i] = char('a' + i % 26);

    auto& server = nodes[leader]->server();

    auto session = server.open_session(1, 1, 0, payload.size(), 0, 0);
    ASSERT_TRUE(session.success) << session.error;

    auto write = server.write_chunk(session.session_id, 0,
                                    std::vector<uint8_t>(payload.begin(), payload.end()));
    ASSERT_TRUE(write.success) << write.error;

    auto complete = server.complete_session(session.session_id, 0);
    ASSERT_TRUE(complete.success) << complete.error;

    // Read from a different node than the one that accepted the upload.
    int reader = (leader + 1) % 3;
    std::vector<uint8_t> data;
    for (int i = 0; i < 500; i++) {
        auto result = nodes[reader]->server().read_file(complete.logical_file_id, 0);
        if (!result.data.empty()) {
            data = std::move(result.data);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(std::string(data.begin(), data.end()), payload)
        << "node " << (reader + 1) << " could not read the replicated file";

    // Tear the cluster down before removing its data directories so the Raft
    // and storage threads are stopped first.
    nodes.clear();
    std::filesystem::remove_all(base);
}

}  // namespace filegroup
