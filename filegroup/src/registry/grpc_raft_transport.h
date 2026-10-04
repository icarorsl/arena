#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "raft/raft.h"
#include "registry.grpc.pb.h"

namespace filegroup {

/// RaftTransport implementation that delivers RequestVote / AppendEntries RPCs
/// to peer registry nodes over gRPC. This is what turns the previously
/// in-process-only Raft node into a real multi-node cluster.
class GrpcRaftTransport : public RaftTransport {
public:
    /// @param timeout_ms Per-RPC deadline. Kept below the election timeout so a
    ///        dead peer cannot stall an election for long.
    explicit GrpcRaftTransport(
        std::shared_ptr<grpc::ChannelCredentials> creds = nullptr,
        int timeout_ms = 100);

    /// Register (or update) the "host:port" address of a peer node.
    void set_peer(uint32_t node_id, const std::string& address);
    void clear_peers();

    RequestVoteReply send_request_vote(uint32_t peer_id, const RequestVoteArgs& args) override;
    AppendEntriesReply send_append_entries(uint32_t peer_id, const AppendEntriesArgs& args) override;

private:
    registry::Registry::Stub* stub_for(uint32_t peer_id);

    std::shared_ptr<grpc::ChannelCredentials> creds_;
    int timeout_ms_;

    std::mutex mutex_;
    std::unordered_map<uint32_t, std::string> addresses_;
    std::unordered_map<uint32_t, std::shared_ptr<grpc::Channel>> channels_;
    std::unordered_map<uint32_t, std::unique_ptr<registry::Registry::Stub>> stubs_;
};

}  // namespace filegroup
