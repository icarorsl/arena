#pragma once

#include <cstdint>
#include <vector>

namespace filegroup {

// ============================================================================
// Chunk Distribution Algorithm
//
// Deterministically assigns each chunk to a primary node + replica nodes
// using only HEALTHY ORIGIN nodes. Ring-based: primary is selected by hash,
// replicas are the next nodes clockwise in the ring.
// ============================================================================

struct ChunkAssignment {
    uint32_t chunk_index;
    uint16_t primary_node_id;
    std::vector<uint16_t> replica_node_ids;
};

/// Compute chunk assignments for all chunks of a file version.
/// @param file_id The file being uploaded
/// @param chunk_count Total number of chunks in the file
/// @param replication_factor Number of replicas per chunk (1 = no replicas)
/// @param healthy_origin_node_ids Sorted list of healthy ORIGIN node IDs
/// @return One ChunkAssignment per chunk
///
/// Algorithm:
///   primary = healthy_nodes[(file_id + chunk_index) % healthy_count]
///   replicas = next (replication_factor - 1) nodes clockwise in ring, skipping primary
std::vector<ChunkAssignment> assign_chunks(
    uint64_t file_id,
    uint32_t chunk_count,
    uint8_t replication_factor,
    const std::vector<uint16_t>& healthy_origin_node_ids);

}  // namespace filegroup
