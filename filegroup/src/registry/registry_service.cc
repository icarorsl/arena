#include "registry/registry_service.h"
#include "common/clock.h"
#include <stdexcept>
#include <algorithm>

namespace filegroup {

// Static instance holder
static RegistryService* g_registry_instance = nullptr;

RegistryService::RegistryService() {
}

RegistryService& RegistryService::instance() {
    if (!g_registry_instance) {
        g_registry_instance = new RegistryService();
    }
    return *g_registry_instance;
}

RegistryResponse RegistryService::register_node(
    uint32_t node_id,
    const std::string& host,
    uint32_t port) {
    
    if (node_id == 0) {
        return {false, "node_id must be > 0"};
    }
    
    if (host.empty()) {
        return {false, "host is empty"};
    }
    
    if (port == 0 || port > 65535) {
        return {false, "port must be 1-65535"};
    }
    
    if (nodes_.find(node_id) != nodes_.end()) {
        return {false, "node_id already registered"};
    }
    
    NodeInfo info;
    info.node_id = node_id;
    info.host = host;
    info.port = port;
    info.status = "healthy";
    info.last_heartbeat_us = now_us();
    
    nodes_[node_id] = info;
    
    return {true, ""};
}

RegistryResponse RegistryService::unregister_node(uint32_t node_id) {
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return {false, "node not found"};
    }
    
    nodes_.erase(it);
    return {true, ""};
}

RegistryResponse RegistryService::heartbeat(uint32_t node_id) {
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return {false, "node not found"};
    }
    
    it->second.last_heartbeat_us = now_us();
    return {true, ""};
}

NodesListResponse RegistryService::list_nodes() {
    NodesListResponse resp;
    
    for (const auto& [node_id, info] : nodes_) {
        resp.nodes.push_back(info);
    }
    
    return resp;
}

NodesListResponse RegistryService::get_node(uint32_t node_id) {
    NodesListResponse resp;
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        resp.error = "node not found";
        return resp;
    }
    
    resp.nodes.push_back(it->second);
    return resp;
}

RegistryResponse RegistryService::set_node_status(
    uint32_t node_id,
    const std::string& status) {
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return {false, "node not found"};
    }
    
    if (status != "healthy" && status != "degraded" && status != "offline") {
        return {false, "invalid status"};
    }
    
    it->second.status = status;
    return {true, ""};
}

}  // namespace filegroup
