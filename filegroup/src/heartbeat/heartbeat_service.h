#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <vector>

namespace filegroup {

/**
 * Node Health Heartbeat Service (Phase 1)
 *
 * Monitors storage node health:
 * 1. Periodic heartbeat collection
 * 2. Node status tracking
 * 3. Failure detection
 * 4. Dead node marking
 */

struct NodeHealthStatus {
    uint32_t node_id;
    bool alive;
    uint64_t last_heartbeat_us;
    uint64_t uptime_us;
    uint64_t disk_used_bytes;
    uint64_t disk_free_bytes;
    uint32_t chunk_count;
    std::string status_message;
};

class HeartbeatService {
public:
    HeartbeatService();
    
    /**
     * Report heartbeat from a node.
     */
    void report_heartbeat(
        uint32_t node_id,
        uint64_t uptime_us,
        uint64_t disk_used_bytes,
        uint64_t disk_free_bytes,
        uint32_t chunk_count
    );
    
    /**
     * Check if node is alive (heartbeat within timeout).
     */
    bool is_node_alive(uint32_t node_id, uint64_t timeout_us = 30000000);  // 30 sec default
    
    /**
     * Get node health status.
     */
    NodeHealthStatus get_node_status(uint32_t node_id);
    
    /**
     * Get all healthy nodes.
     */
    std::vector<uint32_t> get_healthy_nodes(uint64_t timeout_us = 30000000);
    
    /**
     * Get all dead nodes.
     */
    std::vector<uint32_t> get_dead_nodes(uint64_t timeout_us = 30000000);

private:
    std::map<uint32_t, NodeHealthStatus> node_status_;
};

}  // namespace filegroup
