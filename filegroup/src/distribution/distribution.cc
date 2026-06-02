#include "distribution/distribution.h"
#include <stdexcept>
#include <algorithm>

namespace filegroup {

std::vector<ChunkAssignment> assign_chunks(
    uint64_t file_id,
    uint32_t chunk_count,
    uint8_t replication_factor,
    const std::vector<uint16_t>& healthy_origin_node_ids) {

    if (healthy_origin_node_ids.empty()) {
        throw std::invalid_argument("healthy_origin_node_ids is empty");
    }
    if (replication_factor == 0) {
        throw std::invalid_argument("replication_factor must be > 0");
    }

    uint32_t node_count = static_cast<uint32_t>(healthy_origin_node_ids.size());
    std::vector<ChunkAssignment> assignments;
    assignments.reserve(chunk_count);

    for (uint32_t chunk_idx = 0; chunk_idx < chunk_count; ++chunk_idx) {
        ChunkAssignment assignment;
        assignment.chunk_index = chunk_idx;

        uint32_t primary_idx = static_cast<uint32_t>((file_id + chunk_idx) % node_count);
        assignment.primary_node_id = healthy_origin_node_ids[primary_idx];

        uint8_t replica_count = replication_factor - 1;
        for (uint8_t r = 0; r < replica_count; r++) {
            uint32_t replica_idx = (primary_idx + r + 1) % node_count;
            if (replica_idx == primary_idx) continue;
            uint16_t replica_id = healthy_origin_node_ids[replica_idx];
            if (std::find(assignment.replica_node_ids.begin(),
                          assignment.replica_node_ids.end(),
                          replica_id) == assignment.replica_node_ids.end()) {
                assignment.replica_node_ids.push_back(replica_id);
            }
        }
        assignments.push_back(std::move(assignment));
    }
    return assignments;
}

}  // namespace filegroup
