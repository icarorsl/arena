#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace filegroup {

/**
 * Expiry Scanner and Cleanup Service (Phase 1)
 *
 * Manages TTL-based data expiry:
 * 1. Periodic expiry checking
 * 2. Version expiration when TTL exceeded
 * 3. Chunk cleanup from storage nodes
 * 4. Manifest log compaction
 */

struct ExpiryStats {
    uint64_t versions_expired;
    uint64_t chunks_deleted;
    uint64_t storage_freed_bytes;
    uint64_t last_scan_us;
};

class ExpiryService {
public:
    ExpiryService();
    
    /**
     * Run expiry scanning cycle.
     *
     * Checks all versions and files for expiration,
     * marks expired versions, and queues chunks for deletion.
     *
     * Returns:
     *   Statistics about expirations performed
     */
    ExpiryStats run_expiry_cycle();
    
    /**
     * Check if file/version has expired.
     *
     * Returns:
     *   true if past expiration time
     */
    bool is_expired(uint64_t expires_at_us);
    
    /**
     * Get expiry statistics.
     */
    ExpiryStats get_stats() const;
    
    /**
     * Set scan interval.
     */
    void set_scan_interval_us(uint64_t interval_us);

private:
    ExpiryStats stats_;
    uint64_t scan_interval_us_ = 60000000;  // 60 seconds default
};

}  // namespace filegroup
