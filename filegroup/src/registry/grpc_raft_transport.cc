#include "registry/grpc_raft_transport.h"

#include <grpcpp/create_channel.h>

namespace filegroup {

GrpcRaftTransport::GrpcRaftTransport(std::shared_ptr<grpc::ChannelCredentials> creds,
                                     int timeout_ms)
    : creds_(creds ? std::move(creds) : grpc::InsecureChannelCredentials())
    , timeout_ms_(timeout_ms) {}

void GrpcRaftTransport::set_peer(uint32_t node_id, const std::string& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    addresses_[node_id] = address;
    // Force the stub to be rebuilt against the new address on next use.
    stubs_.erase(node_id);
    channels_.erase(node_id);
}

void GrpcRaftTransport::clear_peers() {
    std::lock_guard<std::mutex> lock(mutex_);
    addresses_.clear();
    channels_.clear();
    stubs_.clear();
}

registry::Registry::Stub* GrpcRaftTransport::stub_for(uint32_t peer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto sit = stubs_.find(peer_id);
    if (sit != stubs_.end()) return sit->second.get();

    auto ait = addresses_.find(peer_id);
    if (ait == addresses_.end()) return nullptr;

    auto channel = grpc::CreateChannel(ait->second, creds_);
    auto stub = std::make_unique<registry::Registry::Stub>(channel);
    auto* raw = stub.get();
    channels_[peer_id] = std::move(channel);
    stubs_[peer_id] = std::move(stub);
    return raw;
}

RequestVoteReply GrpcRaftTransport::send_request_vote(uint32_t peer_id, const RequestVoteArgs& args) {
    RequestVoteReply reply{0, false};

    auto* stub = stub_for(peer_id);
    if (!stub) return reply;

    registry::RequestVoteRequest req;
    req.set_term(args.term);
    req.set_candidate_id(args.candidate_id);
    req.set_last_log_index(args.last_log_index);
    req.set_last_log_term(args.last_log_term);

    registry::RequestVoteResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms_));
    auto status = stub->RequestVote(&ctx, req, &resp);
    if (!status.ok()) return reply;

    reply.term = resp.term();
    reply.vote_granted = resp.vote_granted();
    return reply;
}

AppendEntriesReply GrpcRaftTransport::send_append_entries(uint32_t peer_id, const AppendEntriesArgs& args) {
    AppendEntriesReply reply{0, false, 0};

    auto* stub = stub_for(peer_id);
    if (!stub) return reply;

    registry::AppendEntriesRequest req;
    req.set_term(args.term);
    req.set_leader_id(args.leader_id);
    req.set_prev_log_index(args.prev_log_index);
    req.set_prev_log_term(args.prev_log_term);
    req.set_leader_commit(args.leader_commit);
    for (const auto& e : args.entries) {
        auto* le = req.add_entries();
        le->set_term(e.term);
        le->set_index(e.index);
        le->set_entry_type(e.entry_type);
        le->set_data(e.data.data(), e.data.size());
    }

    registry::AppendEntriesResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms_));
    auto status = stub->AppendEntries(&ctx, req, &resp);
    if (!status.ok()) return reply;

    reply.term = resp.term();
    reply.success = resp.success();
    reply.match_index = resp.match_index();
    return reply;
}

}  // namespace filegroup
