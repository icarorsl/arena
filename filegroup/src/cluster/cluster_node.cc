#include "cluster/cluster_node.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "metrics/metrics.h"
#include "registry/grpc_raft_transport.h"
#include "registry/registry_grpc_service.h"
#include "registry/registry_service.h"
#include "storage/storage_grpc_client.h"
#include "storage/storage_grpc_service.h"
#include "storage/storage_node_service.h"

namespace filegroup {

namespace {

/// Extract the port from a "host:port" address. Returns 0 if absent.
int port_of(const std::string& address) {
    auto pos = address.rfind(':');
    if (pos == std::string::npos) return 0;
    try {
        return std::stoi(address.substr(pos + 1));
    } catch (...) {
        return 0;
    }
}

std::string listen_address(const std::string& advertised) {
    int port = port_of(advertised);
    return "0.0.0.0:" + std::to_string(port);
}

}  // namespace

ClusterNode::ClusterNode(const ClusterConfig& config, MetricsServer* metrics,
                         const std::string& data_root, uint16_t node_id)
    : config_(config), node_id_(node_id) {
    // The Raft node persists its log immediately on construction, so the data
    // directory must exist before the registry server is created.
    std::filesystem::create_directories(data_root);

    // ---- Registry membership & addresses --------------------------------
    std::vector<uint32_t> members;
    for (const auto& rn : config_.registry_nodes) {
        members.push_back(rn.id);
        if (rn.id == node_id_) registry_address_ = rn.address;
    }
    if (members.empty()) members.push_back(node_id_);
    if (registry_address_.empty() && !config_.registry_nodes.empty()) {
        registry_address_ = config_.registry_nodes.front().address;
    }

    // ---- gRPC channels/stubs to peer registries -------------------------
    for (const auto& rn : config_.registry_nodes) {
        auto channel = grpc::CreateChannel(rn.address, grpc::InsecureChannelCredentials());
        peer_stubs_[rn.id] = registry::Registry::NewStub(channel);
        peer_channels_[rn.id] = std::move(channel);
    }

    // ---- Raft node with a gRPC transport --------------------------------
    auto transport = std::make_unique<GrpcRaftTransport>();
    for (const auto& rn : config_.registry_nodes) {
        if (rn.id != node_id_) transport->set_peer(rn.id, rn.address);
    }

    auto registry_server = std::make_unique<RegistryServer>(
        node_id_, members, data_root + "/raft.log", 0, std::move(transport));
    local_registry_ = registry_server.get();

    // ---- Registry client (forward writes to the leader) -----------------
    auto registry_client = std::make_unique<RegistryClient>(
        std::vector<RegistryServer*>{registry_server.get()});
    registry_client->set_leader_resolver([this] { return resolve_leader(); });
    registry_client->set_append_forwarder(
        [this](uint32_t leader, uint32_t type, const void* body, uint16_t len) {
            return forward_append(leader, type, body, len);
        });

    // ---- Storage: local server + remote clients -------------------------
    EngineServerComponents components;
    for (const auto& sn : config_.storage_nodes) {
        if (sn.node_id == node_id_) {
            auto server = std::make_unique<StorageServer>(
                sn.node_id, data_root + "/node_" + std::to_string(sn.node_id));
            local_storage_ = server.get();
            storage_address_ = sn.address;
            components.storage_clients.push_back(std::make_unique<StorageClient>(server.get()));
            components.local_storage_servers.push_back(std::move(server));
        } else {
            auto channel = grpc::CreateChannel(sn.address, grpc::InsecureChannelCredentials());
            components.storage_clients.push_back(
                std::make_unique<GrpcStorageClient>(channel, sn.node_id));
            storage_channels_[sn.node_id] = std::move(channel);
        }
    }

    components.registry = std::move(registry_server);
    components.registry_client = std::move(registry_client);
    components.wait_for_leader = false;  // let the cluster elect asynchronously

    // ---- Cluster-internal gRPC servers ----------------------------------
    registry_service_ = std::make_unique<RegistryGrpcService>(
        &local_registry_->raft_node(), &local_registry_->file_index(), node_id_,
        registry_address_);
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort(listen_address(registry_address_),
                                 grpc::InsecureServerCredentials(), &registry_port_);
        builder.RegisterService(registry_service_.get());
        registry_grpc_ = builder.BuildAndStart();
    }

    if (local_storage_) {
        storage_service_ = std::make_unique<StorageGrpcService>(local_storage_);
        grpc::ServerBuilder builder;
        builder.AddListeningPort(listen_address(storage_address_),
                                 grpc::InsecureServerCredentials(), &storage_port_);
        builder.RegisterService(storage_service_.get());
        storage_grpc_ = builder.BuildAndStart();
    }

    // Engine is constructed last so the cluster-internal gRPC servers are
    // already accepting connections before the engine's background services
    // start talking to peers.
    engine_server_ = std::make_unique<EngineServer>(config_, metrics, data_root,
                                                    std::move(components));
}

ClusterNode::~ClusterNode() {
    if (registry_grpc_) registry_grpc_->Shutdown();
    if (storage_grpc_) storage_grpc_->Shutdown();
    registry_service_.reset();
    storage_service_.reset();
    engine_server_.reset();
}

bool ClusterNode::is_leader() const {
    return local_registry_ && local_registry_->is_leader();
}

uint32_t ClusterNode::leader_id() const {
    return local_registry_ ? local_registry_->raft_node().leader_id() : 0;
}

uint32_t ClusterNode::resolve_leader() const {
    if (local_registry_ && local_registry_->is_leader()) return node_id_;

    for (const auto& [id, stub] : peer_stubs_) {
        if (id == node_id_) continue;
        registry::GetLeaderRequest req;
        registry::GetLeaderResponse resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(200));
        if (stub->GetLeader(&ctx, req, &resp).ok() && resp.is_leader()) {
            return id;
        }
    }
    return 0;
}

std::pair<bool, uint64_t> ClusterNode::forward_append(uint32_t leader_id, uint32_t entry_type,
                                                      const void* body, uint16_t body_length) {
    auto it = peer_stubs_.find(leader_id);
    if (it == peer_stubs_.end()) return {false, 0};

    registry::AppendEntryRequest req;
    req.set_group_id(0);
    req.set_entry_type(entry_type);
    req.set_entry_body(body, body_length);

    registry::AppendEntryResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(2000));
    auto status = it->second->AppendEntry(&ctx, req, &resp);
    if (!status.ok() || !resp.success()) return {false, 0};
    return {true, resp.lsn()};
}

}  // namespace filegroup
