#include "distribution/distribution.h"
#include <stdexcept>
#include <set>
#include <algorithm>

namespace filegroup {

std::vector<ChunkPlacement> distribute_chunks(
    uint32_t total_chunks,
    uint8_t replication_factor,
    const std::vector<uint32_t>& available_nodes) {
    
    if (available_nodes.empty()) {
        throw std::invalid_argument("available_nodes is empty");
    }
    
    if (replication_factor == 0) {
        throw std::invalid_argument("replication_factor must be > 0");
    }
    
    if (replication_factor > available_nodes.size()) {
        throw std::invalid_argument(
            "replication_factor (" + std::to_string(replication_factor) + 
            ") > available_nodes.size() (" + std::to_string(available_nodes.size()) + ")"
        );
    }
    
    std::vector<ChunkPlacement> result;
    result.reserve(total_chunks);
    
    // Round-robin placement: for each chunk, assign replication_factor nodes
    // starting from different offsets to balance load
    for (uint32_t chunk_idx = 0; chunk_idx < total_chunks; ++chunk_idx) {
        ChunkPlacement placement;
        placement.chunk_index = chunk_idx;
        placement.node_ids.reserve(replication_factor);
        
        // Start position rotates for each chunk to spread load evenly
        uint32_t start = (chunk_idx * replication_factor) % available_nodes.size();
        
        for (uint8_t rep = 0; rep < replication_factor; ++rep) {
            uint32_t node_idx = (start + rep) % available_nodes.size();
            placement.node_ids.push_back(available_nodes[node_idx]);
        }
        
        result.push_back(placement);
    }
    
    return result;
}

bool validate_distribution(
    const std::vector<ChunkPlacement>& placements,
    uint32_t total_chunks,
    uint8_t replication_factor,
    const std::vector<uint32_t>& available_nodes) {
    
    // Check total count
    if (placements.size() != total_chunks) {
        return false;
    }
    
    std::set<uint32_t> valid_node_ids(available_nodes.begin(), available_nodes.end());
    
    for (uint32_t i = 0; i < placements.size(); ++i) {
        const auto& placement = placements[i];
        
        // Check chunk index matches position
        if (placement.chunk_index != i) {
            return false;
        }
        
        // Check replication factor
        if (placement.node_ids.size() != replication_factor) {
            return false;
        }
        
        // Check no duplicates within chunk
        std::set<uint32_t> chunk_nodes(placement.node_ids.begin(), placement.node_ids.end());
        if (chunk_nodes.size() != replication_factor) {
            return false;
        }
        
        // Check all nodes are valid
        for (uint32_t node_id : placement.node_ids) {
            if (valid_node_ids.find(node_id) == valid_node_ids.end()) {
                return false;
            }
        }
    }
    
    return true;
}

}  // namespace filegroup
