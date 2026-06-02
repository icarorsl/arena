#pragma once

#include <cstdint>
#include <vector>
#include <map>

namespace filegroup {

/**
 * Chunk distribution algorithm for Phase 1.
 *
 * Distributes chunks across storage nodes for replication.
 * Uses even distribution with deterministic placement.
 */

struct ChunkPlacement {
    uint32_t chunk_index;
    std::vector<uint32_t> node_ids;  // List of node IDs for replication
};

/**
 * Distributes chunks across available nodes.
 *
 * Algorithm:
 * - For each chunk, assign replication_factor nodes
 * - Use round-robin across available nodes for even load balancing
 * - Deterministic: same input always produces same output
 *
 * Parameters:
 *   total_chunks: Number of chunks to distribute
 *   replication_factor: Number of copies per chunk
 *   available_nodes: List of available node IDs
 *
 * Returns:
 *   Vector of ChunkPlacement, one per chunk
 *
 * Throws:
 *   std::invalid_argument if replication_factor > available_nodes.size()
 */
std::vector<ChunkPlacement> distribute_chunks(
    uint32_t total_chunks,
    uint8_t replication_factor,
    const std::vector<uint32_t>& available_nodes
);

/**
 * Validates a distribution plan.
 *
 * Checks:
 * - All chunks have exactly replication_factor nodes
 * - No duplicate nodes within a chunk
 * - All node IDs are in available_nodes
 * - All chunks 0..total_chunks-1 are present
 */
bool validate_distribution(
    const std::vector<ChunkPlacement>& placements,
    uint32_t total_chunks,
    uint8_t replication_factor,
    const std::vector<uint32_t>& available_nodes
);

}  // namespace filegroup
