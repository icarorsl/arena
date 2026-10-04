#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "common/crc32c.h"
#include "manifest/manifest.h"
#include "registry/grpc_raft_transport.h"
#include "registry/registry_grpc_service.h"
#include "registry/registry_service.h"
#include "storage/storage_grpc_client.h"
#include "storage/storage_grpc_service.h"

namespace filegroup {
namespace {

struct Node {
    uint32_t id = 0;
    int port = 0;
    std::string log_path;
    GrpcRaftTransport* transport = nullptr;
    std::unique_ptr<RegistryServer> server;
    std::unique_ptr<RegistryGrpcService> service;
    std::unique_ptr<grpc::Server> grpc;
};

}  // namespace

// A 3-node registry cluster that talks purely over gRPC must elect a leader
// and replicate a manifest entry to every node's state machine.
TEST(ClusterReplication, RaftReplicatesOverGrpc) {
    const std::vector<uint32_t> members = {1, 2, 3};
    std::vector<std::unique_ptr<Node>> nodes;

    for (uint32_t id : members) {
        auto n = std::make_unique<Node>();
        n->id = id;
        n->log_path = "/tmp/cluster_raft_" + std::to_string(id) + "_" +
                      std::to_string(rand()) + ".log";
        std::remove(n->log_path.c_str());

        auto transport = std::make_unique<GrpcRaftTransport>();
        n->transport = transport.get();
        n->server = std::make_unique<RegistryServer>(id, members, n->log_path, 0,
                                                     std::move(transport));
        n->service = std::make_unique<RegistryGrpcService>(
            &n->server->raft_node(), &n->server->file_index(), id, "");

        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &n->port);
        builder.RegisterService(n->service.get());
        n->grpc = builder.BuildAndStart();
        ASSERT_TRUE(n->grpc) << "failed to start gRPC server for node " << id;
        ASSERT_GT(n->port, 0);
        nodes.push_back(std::move(n));
    }

    // Ports are known now — wire each node's transport to its peers.
    for (auto& n : nodes) {
        for (auto& peer : nodes) {
            if (peer->id == n->id) continue;
            n->transport->set_peer(peer->id, "127.0.0.1:" + std::to_string(peer->port));
        }
    }

    // Wait for exactly one leader.
    RegistryServer* leader = nullptr;
    for (int i = 0; i < 500 && !leader; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        int leaders = 0;
        RegistryServer* candidate = nullptr;
        for (auto& n : nodes) {
            if (n->server->is_leader()) {
                leaders++;
                candidate = n->server.get();
            }
        }
        leader = (leaders == 1) ? candidate : nullptr;
    }
    ASSERT_NE(leader, nullptr) << "no single leader elected over gRPC";

    // Propose a table creation through the leader's Raft log.
    TableCreatedEntry entry{};
    entry.table_id = 99;
    entry.group_id = 1;
    std::snprintf(entry.name, sizeof(entry.name), "replicated");
    entry.chunk_size = 65536;
    entry.replication_factor = 3;
    entry.max_versions = 5;
    entry.file_expires_in_days = 0;
    entry.expiry_granularity = 0;

    auto [ok, lsn] = leader->raft_node().propose(
        static_cast<uint32_t>(ManifestEntryType::TABLE_CREATED), &entry, sizeof(entry));
    ASSERT_TRUE(ok) << "leader failed to commit proposal";
    EXPECT_EQ(lsn, 1u);

    auto has_table = [](RegistryServer* s) {
        for (const auto& t : s->file_index().get_tables()) {
            if (t.table_id == 99) return true;
        }
        return false;
    };

    for (int i = 0; i < 500; i++) {
        bool all = true;
        for (auto& n : nodes) all = all && has_table(n->server.get());
        if (all) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (auto& n : nodes) {
        EXPECT_TRUE(has_table(n->server.get()))
            << "node " << n->id << " did not apply the replicated entry";
    }

    for (auto& n : nodes) n->grpc->Shutdown();
}

// A chunk stored on a remote storage node over gRPC must be fetchable back
// byte-for-byte — the transport that chunk replication relies on.
TEST(ClusterReplication, StorageOverGrpc) {
    std::string dir = "/tmp/storage_grpc_" + std::to_string(rand());
    std::filesystem::remove_all(dir);

    StorageServer server(7, dir);
    StorageGrpcService service(&server);

    int port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    auto grpc_server = builder.BuildAndStart();
    ASSERT_TRUE(grpc_server);
    ASSERT_GT(port, 0);

    auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(port),
                                       grpc::InsecureChannelCredentials());
    GrpcStorageClient client(channel, 7);
    ASSERT_FALSE(client.is_local());
    EXPECT_EQ(client.node_id(), 7);
    EXPECT_TRUE(client.ping());

    const std::string payload = "hello-replication";
    auto stored = client.store_chunk(
        1, 0, 1, 1,
        reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
        crc32c(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()),
        false, 0, ExpiryGranularity::UNSET);
    ASSERT_TRUE(stored.success) << stored.error;
    EXPECT_FALSE(stored.segment_file.empty());

    auto fetched = client.fetch_chunk(stored.segment_file, stored.offset, payload.size());
    ASSERT_TRUE(fetched.success) << fetched.error;
    EXPECT_EQ(std::string(fetched.data.begin(), fetched.data.end()), payload);

    grpc_server->Shutdown();
    std::filesystem::remove_all(dir);
}


}  // namespace filegroup
