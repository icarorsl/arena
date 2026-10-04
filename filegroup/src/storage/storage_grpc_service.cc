#include "storage/storage_grpc_service.h"

#include "common/clock.h"

namespace filegroup {

namespace {

ExpiryGranularity granularity_from_proto(uint32_t v) {
    switch (v) {
        case 0: return ExpiryGranularity::DAY;
        case 1: return ExpiryGranularity::WEEK;
        case 2: return ExpiryGranularity::MONTH;
        default: return ExpiryGranularity::UNSET;
    }
}

}  // namespace

StorageGrpcService::StorageGrpcService(StorageServer* server) : server_(server) {}

grpc::Status StorageGrpcService::StoreChunk(grpc::ServerContext*,
                                            const storage::StoreChunkRequest* req,
                                            storage::StoreChunkResponse* resp) {
    auto result = server_->store_chunk(
        req->file_id(), req->chunk_index(), req->group_id(), req->table_id(),
        reinterpret_cast<const uint8_t*>(req->data().data()), req->data().size(),
        req->chunk_checksum(), req->is_encrypted(), req->expires_at_us(),
        granularity_from_proto(req->page_granularity()));

    resp->set_node_id(server_->node_id());
    if (result.success) {
        resp->set_segment_file(result.segment_file);
        resp->set_offset(result.offset);
    } else {
        resp->set_error(result.error);
    }
    return grpc::Status::OK;
}

grpc::Status StorageGrpcService::FetchChunk(grpc::ServerContext*,
                                            const storage::FetchChunkRequest* req,
                                            storage::FetchChunkResponse* resp) {
    auto result = server_->fetch_chunk(req->segment_file(), req->offset(), req->length());
    if (result.success) {
        resp->set_data(result.data.data(), result.data.size());
    } else {
        resp->set_error(result.error);
    }
    return grpc::Status::OK;
}

grpc::Status StorageGrpcService::DeleteChunk(grpc::ServerContext*,
                                             const storage::DeleteChunkRequest* req,
                                             storage::DeleteChunkResponse* resp) {
    resp->set_success(server_->delete_chunk(req->file_id(), req->chunk_index()));
    return grpc::Status::OK;
}

grpc::Status StorageGrpcService::DeletePage(grpc::ServerContext*,
                                            const storage::DeletePageRequest* req,
                                            storage::DeletePageResponse* resp) {
    resp->set_success(server_->delete_page(req->page_path()));
    return grpc::Status::OK;
}

grpc::Status StorageGrpcService::Ping(grpc::ServerContext*,
                                      const storage::PingRequest*,
                                      storage::PingResponse* resp) {
    resp->set_alive(server_->ping());
    resp->set_timestamp_us(now_us());
    return grpc::Status::OK;
}

grpc::Status StorageGrpcService::ReportSegments(grpc::ServerContext*,
                                                const storage::ReportSegmentsRequest*,
                                                storage::ReportSegmentsResponse* resp) {
    for (const auto& inv : server_->report_segments()) {
        auto* seg = resp->add_segments();
        seg->set_segment_file(inv.segment_file);
        seg->set_total_bytes(inv.total_bytes);
        seg->set_used_bytes(inv.used_bytes);
        for (const auto& [file_id, chunk_index] : inv.chunks) {
            auto* c = seg->add_chunks();
            c->set_file_id(file_id);
            c->set_chunk_index(chunk_index);
        }
    }
    return grpc::Status::OK;
}

}  // namespace filegroup
