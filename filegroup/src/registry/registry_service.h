#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <map>

namespace filegroup {

/**
 * Registry RPC Service (Phase 1 Mock Implementation)
 *
 * In-process registry service for coordinating storage nodes.
 * This is a mock that implements the RPC interface as C++ methods.
 * Will be replaced with actual gRPC service in future.
 */

struct NodeInfo {
    uint32_t node_id;
    std::string host;
    uint32_t port;
    std::string status;  // "healthy", "degraded", "offline"
    uint64_t last_heartbeat_us;
};

struct RegistryResponse {
    bool success;
    std::string error;
};

struct NodesListResponse {
    std::vector<NodeInfo> nodes;
    std::string error;
};

class RegistryService {
public:
    /**
     * Singleton instance.
     */
    static RegistryService& instance();
    
    /**
     * Register a storage node with the registry.
     *
     * Parameters:
     *   node_id: Unique node identifier
     *   host: Node hostname or IP address
     *   port: Node gRPC port
     *
     * Returns:
     *   Response with success flag
     */
    RegistryResponse register_node(
        uint32_t node_id,
        const std::string& host,
        uint32_t port
    );
    
    /**
     * Unregister a storage node.
     */
    RegistryResponse unregister_node(uint32_t node_id);
    
    /**
     * Update node heartbeat timestamp.
     */
    RegistryResponse heartbeat(uint32_t node_id);
    
    /**
     * Get list of all registered nodes.
     */
    NodesListResponse list_nodes();
    
    /**
     * Get specific node info.
     */
    NodesListResponse get_node(uint32_t node_id);
    
    /**
     * Mark node as healthy/degraded/offline.
     */
    RegistryResponse set_node_status(
        uint32_t node_id,
        const std::string& status
    );

private:
    RegistryService();
    std::map<uint32_t, NodeInfo> nodes_;
};

}  // namespace filegroup
