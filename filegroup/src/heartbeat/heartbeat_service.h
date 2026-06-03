#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/types.h"

namespace filegroup {

class StorageClient;
class RegistryClient;

// ============================================================================
// HeartbeatService — actively pings storage nodes, HEALTHY/SUSPECT/DEAD FSM
// ============================================================================

class HeartbeatService {
public:
    HeartbeatService(const std::vector<StorageClient*>& clients,
                     RegistryClient* registry,
                     uint64_t interval_sec = 5);
    ~HeartbeatService();

    void start();
    void stop();

    NodeState get_state(uint16_t node_id) const;

private:
    void run();

    std::vector<StorageClient*> clients_;
    RegistryClient* registry_;
    uint64_t interval_us_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex state_mutex_;
    std::unordered_map<uint16_t, NodeState> states_;
    std::unordered_map<uint16_t, uint32_t> miss_counts_;
};

}  // namespace filegroup
