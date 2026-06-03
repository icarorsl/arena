#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "common/types.h"

namespace filegroup {

class RegistryClient;

// ============================================================================
// ExpiryService — background scanner that marks expired versions as DELETED
// ============================================================================

class ExpiryService {
public:
    ExpiryService(RegistryClient* registry, uint64_t interval_sec = 60);
    ~ExpiryService();

    void start();
    void stop();

    // Stats
    uint64_t versions_expired() const { return versions_expired_; }
    uint64_t last_scan_us() const { return last_scan_us_; }

private:
    void run();
    void scan();

    RegistryClient* registry_;
    uint64_t interval_us_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex stats_mutex_;
    uint64_t versions_expired_ = 0;
    uint64_t last_scan_us_ = 0;
};

}  // namespace filegroup
