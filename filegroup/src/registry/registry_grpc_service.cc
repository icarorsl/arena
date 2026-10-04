#include "registry/registry_grpc_service.h"

namespace filegroup {

RegistryGrpcService::RegistryGrpcService(RaftNode* raft, FileIndex* index,
                                         uint32_t node_id, std::string address)
    : raft_(raft), index_(index), node_id_(node_id), address_(std::move(address)) {}

grpc::Status RegistryGrpcService::RequestVote(grpc::ServerContext*,
                                              const registry::RequestVoteRequest* req,
                                              registry::RequestVoteResponse* resp) {
    RequestVoteArgs args;
    args.term = req->term();
    args.candidate_id = req->candidate_id();
    args.last_log_index = req->last_log_index();
    args.last_log_term = req->last_log_term();

    auto reply = raft_->handle_request_vote(args);
    resp->set_term(reply.term);
    resp->set_vote_granted(reply.vote_granted);
    return grpc::Status::OK;
}

grpc::Status RegistryGrpcService::AppendEntries(grpc::ServerContext*,
                                                const registry::AppendEntriesRequest* req,
                                                registry::AppendEntriesResponse* resp) {
    AppendEntriesArgs args;
    args.term = req->term();
    args.leader_id = req->leader_id();
    args.prev_log_index = req->prev_log_index();
    args.prev_log_term = req->prev_log_term();
    args.leader_commit = req->leader_commit();
    for (const auto& le : req->entries()) {
        RaftLogEntry entry;
        entry.term = le.term();
        entry.index = le.index();
        entry.entry_type = le.entry_type();
        entry.data.assign(le.data().begin(), le.data().end());
        args.entries.push_back(std::move(entry));
    }

    auto reply = raft_->handle_append_entries(args);
    resp->set_term(reply.term);
    resp->set_success(reply.success);
    resp->set_match_index(reply.match_index);
    return grpc::Status::OK;
}

grpc::Status RegistryGrpcService::GetLeader(grpc::ServerContext*,
                                            const registry::GetLeaderRequest*,
                                            registry::GetLeaderResponse* resp) {
    resp->set_leader_node_id(raft_->leader_id());
    resp->set_is_leader(raft_->is_leader());
    return grpc::Status::OK;
}

grpc::Status RegistryGrpcService::AppendEntry(grpc::ServerContext*,
                                              const registry::AppendEntryRequest* req,
                                              registry::AppendEntryResponse* resp) {
    if (!raft_->is_leader()) {
        resp->set_success(false);
        resp->set_error("not leader");
        resp->set_leader_address(address_);
        return grpc::Status::OK;
    }

    const std::string& body = req->entry_body();
    auto [ok, lsn] = raft_->propose(req->entry_type(), body.data(),
                                    static_cast<uint16_t>(body.size()));
    resp->set_success(ok);
    resp->set_lsn(lsn);
    if (!ok) resp->set_error("raft propose failed");
    return grpc::Status::OK;
}

grpc::Status RegistryGrpcService::GetFile(grpc::ServerContext*,
                                          const registry::GetFileRequest* req,
                                          registry::GetFileResponse* resp) {
    auto* file = index_->get_file(req->logical_file_id());
    if (!file) {
        resp->set_error("not found");
        return grpc::Status::OK;
    }
    auto* info = resp->mutable_file();
    info->set_logical_file_id(file->logical_file_id);
    info->set_table_id(file->table_id);
    info->set_group_id(file->group_id);
    info->set_latest_complete_version(file->latest_complete_version);
    info->set_next_version_number(file->next_version_number);
    info->set_state(0);
    return grpc::Status::OK;
}

grpc::Status RegistryGrpcService::ListFiles(grpc::ServerContext*,
                                            const registry::ListFilesRequest* req,
                                            registry::ListFilesResponse* resp) {
    auto files = index_->list_files(static_cast<uint16_t>(req->table_id()), req->group_id());
    for (const auto& f : files) {
        auto* info = resp->add_files();
        info->set_logical_file_id(f.logical_file_id);
        info->set_table_id(f.table_id);
        info->set_group_id(f.group_id);
        info->set_latest_complete_version(f.latest_complete_version);
        info->set_next_version_number(f.next_version_number);
        info->set_state(0);
    }
    return grpc::Status::OK;
}

}  // namespace filegroup
