#pragma once

#include <memory>

#include <grpcpp/grpcpp.h>

#include "storage/storage_node_service.h"
#include "storage.grpc.pb.h"

namespace filegroup {

/// StorageClient that talks to a StorageServer on another host over gRPC.
/// Enables chunk-level replication across nodes.
class GrpcStorageClient : public StorageClient {
public:
    GrpcStorageClient(std::shared_ptr<grpc::Channel> channel, uint16_t node_id,
                      int timeout_ms = 5000);

    StoreChunkResult store_chunk(
        uint64_t file_id, uint32_t chunk_index,
        uint32_t group_id, uint32_t table_id,
        const uint8_t* data, uint64_t size,
        uint32_t chunk_checksum, bool is_encrypted,
        uint64_t expires_at_us = 0,
        ExpiryGranularity page_granularity = ExpiryGranularity::UNSET) override;

    FetchChunkResult fetch_chunk(const std::string& segment_file,
                                 uint64_t offset, uint64_t length) override;

    bool delete_chunk(uint64_t file_id, uint32_t chunk_index) override;
    bool delete_page(const std::string& page_path) override;
    void invalidate_segment(uint32_t group_id, uint32_t table_id) override;
    bool ping() override;
    uint16_t node_id() const override;
    std::vector<std::string> list_segments() const override;

    bool is_local() const override { return false; }
    StorageServer* server() override { return nullptr; }

private:
    std::unique_ptr<storage::StorageNode::Stub> stub_;
    int timeout_ms_;
};

}  // namespace filegroup
