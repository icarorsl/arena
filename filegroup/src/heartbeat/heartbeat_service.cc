#include "heartbeat/heartbeat_service.h"
#include "common/clock.h"

namespace filegroup {

HeartbeatService::HeartbeatService() {}

void HeartbeatService::report_heartbeat(
    uint32_t node_id,
    uint64_t uptime_us,
    uint64_t disk_used_bytes,
    uint64_t disk_free_bytes,
    uint32_t chunk_count) {
    
    NodeHealthStatus& status = node_status_[node_id];
    status.node_id = node_id;
    status.alive = true;
    status.last_heartbeat_us = now_us();
    status.uptime_us = uptime_us;
    status.disk_used_bytes = disk_used_bytes;
    status.disk_free_bytes = disk_free_bytes;
    status.chunk_count = chunk_count;
    status.status_message = "ok";
}

bool HeartbeatService::is_node_alive(uint32_t node_id, uint64_t timeout_us) {
    auto it = node_status_.find(node_id);
    if (it == node_status_.end()) {
        return false;
    }
    
    uint64_t now = now_us();
    uint64_t time_since_heartbeat = now - it->second.last_heartbeat_us;
    
    return time_since_heartbeat <= timeout_us;
}

NodeHealthStatus HeartbeatService::get_node_status(uint32_t node_id) {
    auto it = node_status_.find(node_id);
    if (it == node_status_.end()) {
        NodeHealthStatus empty;
        empty.node_id = node_id;
        empty.alive = false;
        empty.status_message = "unknown";
        return empty;
    }
    
    return it->second;
}

std::vector<uint32_t> HeartbeatService::get_healthy_nodes(uint64_t timeout_us) {
    std::vector<uint32_t> healthy;
    
    for (const auto& [node_id, status] : node_status_) {
        if (is_node_alive(node_id, timeout_us)) {
            healthy.push_back(node_id);
        }
    }
    
    return healthy;
}

std::vector<uint32_t> HeartbeatService::get_dead_nodes(uint64_t timeout_us) {
    std::vector<uint32_t> dead;
    
    for (const auto& [node_id, status] : node_status_) {
        if (!is_node_alive(node_id, timeout_us)) {
            dead.push_back(node_id);
        }
    }
    
    return dead;
}

}  // namespace filegroup
