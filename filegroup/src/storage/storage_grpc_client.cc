#include "storage/storage_grpc_client.h"

#include <chrono>

namespace filegroup {

GrpcStorageClient::GrpcStorageClient(std::shared_ptr<grpc::Channel> channel, uint16_t node_id,
                                     int timeout_ms)
    : stub_(storage::StorageNode::NewStub(std::move(channel))), timeout_ms_(timeout_ms) {
    node_id_ = node_id;
}

StoreChunkResult GrpcStorageClient::store_chunk(
    uint64_t file_id, uint32_t chunk_index,
    uint32_t group_id, uint32_t table_id,
    const uint8_t* data, uint64_t size,
    uint32_t chunk_checksum, bool is_encrypted,
    uint64_t expires_at_us, ExpiryGranularity page_granularity)
{
    StoreChunkResult result;

    storage::StoreChunkRequest req;
    req.set_file_id(file_id);
    req.set_chunk_index(chunk_index);
    req.set_group_id(group_id);
    req.set_table_id(table_id);
    req.set_data(data, size);
    req.set_chunk_checksum(chunk_checksum);
    req.set_is_encrypted(is_encrypted);
    req.set_expires_at_us(expires_at_us);
    req.set_page_granularity(static_cast<uint32_t>(page_granularity));

    storage::StoreChunkResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms_));
    auto status = stub_->StoreChunk(&ctx, req, &resp);
    if (!status.ok()) {
        result.error = status.error_message();
        return result;
    }

    if (!resp.error().empty()) {
        result.error = resp.error();
        return result;
    }

    result.success = true;
    result.segment_file = resp.segment_file();
    result.offset = resp.offset();
    return result;
}

FetchChunkResult GrpcStorageClient::fetch_chunk(const std::string& segment_file,
                                                uint64_t offset, uint64_t length) {
    FetchChunkResult result;

    storage::FetchChunkRequest req;
    req.set_segment_file(segment_file);
    req.set_offset(offset);
    req.set_length(length);

    storage::FetchChunkResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms_));
    auto status = stub_->FetchChunk(&ctx, req, &resp);
    if (!status.ok()) {
        result.error = status.error_message();
        return result;
    }
    if (!resp.error().empty()) {
        result.error = resp.error();
        return result;
    }

    result.success = true;
    result.data.assign(resp.data().begin(), resp.data().end());
    return result;
}

bool GrpcStorageClient::delete_chunk(uint64_t file_id, uint32_t chunk_index) {
    storage::DeleteChunkRequest req;
    req.set_file_id(file_id);
    req.set_chunk_index(chunk_index);
    req.set_node_id(node_id_);

    storage::DeleteChunkResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms_));
    auto status = stub_->DeleteChunk(&ctx, req, &resp);
    return status.ok() && resp.success();
}

bool GrpcStorageClient::delete_page(const std::string& page_path) {
    storage::DeletePageRequest req;
    req.set_page_path(page_path);

    storage::DeletePageResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms_));
    auto status = stub_->DeletePage(&ctx, req, &resp);
    return status.ok() && resp.success();
}

void GrpcStorageClient::invalidate_segment(uint32_t, uint32_t) {
    // No-op: remote segments are re-opened by the storage node itself.
}

bool GrpcStorageClient::ping() {
    storage::PingRequest req;
    storage::PingResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms_));
    auto status = stub_->Ping(&ctx, req, &resp);
    return status.ok() && resp.alive();
}

uint16_t GrpcStorageClient::node_id() const { return node_id_; }

std::vector<std::string> GrpcStorageClient::list_segments() const {
    return {};
}

}  // namespace filegroup
