#pragma once

#include <cstdint>
#include <string>

#include <grpcpp/grpcpp.h>

#include "manifest/file_index.h"
#include "raft/raft.h"
#include "registry.grpc.pb.h"

namespace filegroup {

/// gRPC front-end for a registry (Raft) node. Exposes the Raft RPCs
/// (RequestVote / AppendEntries) plus the client-facing metadata operations
/// (AppendEntry / GetFile / ListFiles / GetLeader) so engines on other hosts
/// can forward writes to the leader and read replicated metadata.
class RegistryGrpcService final : public registry::Registry::Service {
public:
    RegistryGrpcService(RaftNode* raft, FileIndex* index, uint32_t node_id, std::string address);

    grpc::Status RequestVote(grpc::ServerContext* ctx,
                             const registry::RequestVoteRequest* req,
                             registry::RequestVoteResponse* resp) override;

    grpc::Status AppendEntries(grpc::ServerContext* ctx,
                               const registry::AppendEntriesRequest* req,
                               registry::AppendEntriesResponse* resp) override;

    grpc::Status GetLeader(grpc::ServerContext* ctx,
                           const registry::GetLeaderRequest* req,
                           registry::GetLeaderResponse* resp) override;

    grpc::Status AppendEntry(grpc::ServerContext* ctx,
                             const registry::AppendEntryRequest* req,
                             registry::AppendEntryResponse* resp) override;

    grpc::Status GetFile(grpc::ServerContext* ctx,
                         const registry::GetFileRequest* req,
                         registry::GetFileResponse* resp) override;

    grpc::Status ListFiles(grpc::ServerContext* ctx,
                           const registry::ListFilesRequest* req,
                           registry::ListFilesResponse* resp) override;

private:
    RaftNode* raft_;
    FileIndex* index_;
    uint32_t node_id_;
    std::string address_;
};

}  // namespace filegroup
