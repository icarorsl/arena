#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "common/types.h"
#include "distribution/distribution.h"
namespace filegroup {
struct UploadSession {
    uint64_t session_id=0,file_id=0,logical_file_id=0,created_at_us=0,last_activity_us=0,resolved_chunk_size=0,resolved_expires_at=0,total_bytes=0;
    uint16_t table_id=0; uint32_t group_id=0,version_number=0,expected_chunks=0,content_checksum=0;
    VersionState state=VersionState::UPLOADING;
    uint8_t resolved_replication=0;
    uint32_t resolved_max_versions=0;
    EncryptionAlgo resolved_encryption=EncryptionAlgo::NONE;
    SegmentType segment_type=SegmentType::STANDARD;
    std::string page_bucket;
    std::set<uint32_t> confirmed_chunks;
    std::vector<ChunkAssignment> chunk_assignments;
};
}
