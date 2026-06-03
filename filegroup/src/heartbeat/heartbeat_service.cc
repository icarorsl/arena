#include "heartbeat/heartbeat_service.h"

#include <chrono>
#include <iostream>

#include "common/clock.h"
#include "registry/registry_service.h"
#include "storage/storage_node_service.h"

namespace filegroup {

HeartbeatService::HeartbeatService(const std::vector<StorageClient*>& clients,
                                   RegistryClient* registry,
                                   uint64_t interval_sec)
    : clients_(clients)
    , registry_(registry)
    , interval_us_(interval_sec * 1'000'000)
{
}

HeartbeatService::~HeartbeatService() {
    stop();
}

void HeartbeatService::start() {
    running_ = true;
    thread_ = std::thread(&HeartbeatService::run, this);
}

void HeartbeatService::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

NodeState HeartbeatService::get_state(uint16_t node_id) const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    auto it = states_.find(node_id);
    return (it != states_.end()) ? it->second : NodeState::HEALTHY;
}

void HeartbeatService::run() {
    while (running_) {
        for (auto* client : clients_) {
            if (!client) continue;
            uint16_t nid = client->node_id();

            bool alive = client->ping();

            std::lock_guard<std::mutex> lk(state_mutex_);
            auto& state = states_[nid];
            auto& misses = miss_counts_[nid];

            if (alive) {
                if (state == NodeState::SUSPECT || state == NodeState::DEAD) {
                    std::cout << "[heartbeat] node " << nid << " recovered → HEALTHY\n";
                    state = NodeState::HEALTHY;
                    misses = 0;
                    registry_->report_node_health(nid, NodeState::HEALTHY);
                } else {
                    misses = 0;
                }
            } else {
                misses++;
                if (misses >= 3 && state == NodeState::HEALTHY) {
                    state = NodeState::SUSPECT;
                    std::cout << "[heartbeat] node " << nid << " → SUSPECT (" << misses << " misses)\n";
                    registry_->report_node_health(nid, NodeState::SUSPECT);
                } else if (misses >= 30 && state == NodeState::SUSPECT) {
                    state = NodeState::DEAD;
                    std::cout << "[heartbeat] node " << nid << " → DEAD (" << misses << " misses)\n";
                    registry_->report_node_health(nid, NodeState::DEAD);
                }
            }
        }

        // Sleep in small chunks so we can stop quickly
        for (uint64_t i = 0; i < interval_us_ && running_; i += 500'000) {
            std::this_thread::sleep_for(std::chrono::microseconds(500'000));
        }
    }
}

}  // namespace filegroup
