#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "config/config.h"
#include "engine/engine_service.h"
#include "registry.grpc.pb.h"
#include "storage.grpc.pb.h"

namespace filegroup {

class RegistryServer;
class RegistryGrpcService;
class StorageGrpcService;
class StorageServer;
class MetricsServer;

/// Runs one node of a real multi-node cluster in a single process:
///   * a Raft registry node whose peer RPCs travel over gRPC,
///   * the local storage node, plus gRPC clients to the remote storage nodes,
///   * the engine built on top of those.
///
/// The node's client-facing engine gRPC API is reachable through server();
/// this class only owns the cluster-internal plumbing.
class ClusterNode {
public:
    ClusterNode(const ClusterConfig& config, MetricsServer* metrics,
                const std::string& data_root, uint16_t node_id);
    ~ClusterNode();

    EngineServer& server() { return *engine_server_; }
    uint16_t node_id() const { return node_id_; }
    bool is_leader() const;
    /// Current Raft leader's node id (0 if unknown).
    uint32_t leader_id() const;

    /// Ports actually bound for the cluster-internal gRPC servers.
    int registry_port() const { return registry_port_; }
    int storage_port() const { return storage_port_; }

private:
    /// Find the current Raft leader by asking self and then each peer.
    uint32_t resolve_leader() const;
    /// Send a proposal to a remote leader's registry gRPC service.
    std::pair<bool, uint64_t> forward_append(uint32_t leader_id, uint32_t entry_type,
                                             const void* body, uint16_t body_length);

    ClusterConfig config_;
    uint16_t node_id_;
    std::string registry_address_;
    std::string storage_address_;

    RegistryServer* local_registry_ = nullptr;
    StorageServer* local_storage_ = nullptr;

    std::unordered_map<uint32_t, std::shared_ptr<grpc::Channel>> peer_channels_;
    std::unordered_map<uint32_t, std::unique_ptr<registry::Registry::Stub>> peer_stubs_;
    std::unordered_map<uint32_t, std::shared_ptr<grpc::Channel>> storage_channels_;

    int registry_port_ = 0;
    int storage_port_ = 0;

    std::unique_ptr<RegistryGrpcService> registry_service_;
    std::unique_ptr<StorageGrpcService> storage_service_;
    std::unique_ptr<grpc::Server> registry_grpc_;
    std::unique_ptr<grpc::Server> storage_grpc_;

    std::unique_ptr<EngineServer> engine_server_;
};

}  // namespace filegroup
