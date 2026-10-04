#pragma once

#include <string>

#include <grpcpp/grpcpp.h>

#include "storage/storage_node_service.h"
#include "storage.grpc.pb.h"

namespace filegroup {

/// gRPC front-end for a StorageServer so chunks can be stored/fetched on a
/// different host than the engine that coordinates the upload.
class StorageGrpcService final : public storage::StorageNode::Service {
public:
    explicit StorageGrpcService(StorageServer* server);

    grpc::Status StoreChunk(grpc::ServerContext* ctx,
                            const storage::StoreChunkRequest* req,
                            storage::StoreChunkResponse* resp) override;

    grpc::Status FetchChunk(grpc::ServerContext* ctx,
                            const storage::FetchChunkRequest* req,
                            storage::FetchChunkResponse* resp) override;

    grpc::Status DeleteChunk(grpc::ServerContext* ctx,
                             const storage::DeleteChunkRequest* req,
                             storage::DeleteChunkResponse* resp) override;

    grpc::Status DeletePage(grpc::ServerContext* ctx,
                            const storage::DeletePageRequest* req,
                            storage::DeletePageResponse* resp) override;

    grpc::Status Ping(grpc::ServerContext* ctx,
                      const storage::PingRequest* req,
                      storage::PingResponse* resp) override;

    grpc::Status ReportSegments(grpc::ServerContext* ctx,
                                const storage::ReportSegmentsRequest* req,
                                storage::ReportSegmentsResponse* resp) override;

private:
    StorageServer* server_;
};

}  // namespace filegroup
