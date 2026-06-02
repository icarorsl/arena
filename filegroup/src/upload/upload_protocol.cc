#include "upload/upload_protocol.h"

namespace filegroup {

UploadProtocol::UploadProtocol() : next_session_id_(1) {}

UploadSession UploadProtocol::create_session(const std::string&, uint64_t, const SessionConfig&) {
    return {};
}

ChunkUploadResponse UploadProtocol::upload_chunk(const ChunkUploadRequest&) {
    return {};
}

bool UploadProtocol::finalize_session(uint32_t) {
    return false;
}

}  // namespace filegroup
