#include "expiry/expiry_service.h"
#include "common/clock.h"

namespace filegroup {

ExpiryService::ExpiryService() : stats_{0, 0, 0, 0} {}

ExpiryStats ExpiryService::run_expiry_cycle() {
    stats_.last_scan_us = now_us();
    
    // Phase 1: simulate expiry scanning
    // In production, would:
    // 1. Query file index for versions
    // 2. Check each version's expiration
    // 3. Mark expired versions
    // 4. Queue chunks for deletion
    
    stats_.versions_expired = 0;
    stats_.chunks_deleted = 0;
    stats_.storage_freed_bytes = 0;
    
    return stats_;
}

bool ExpiryService::is_expired(uint64_t expires_at_us) {
    return now_us() > expires_at_us;
}

ExpiryStats ExpiryService::get_stats() const {
    return stats_;
}

void ExpiryService::set_scan_interval_us(uint64_t interval_us) {
    scan_interval_us_ = interval_us;
}

}  // namespace filegroup
