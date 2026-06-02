#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace filegroup {

/**
 * Background Scrubbing Service (Phase 1)
 *
 * Continuously verifies data integrity:
 * 1. Periodic chunk checksums
 * 2. Replication factor verification
 * 3. Corrupted chunk detection
 * 4. Automatic repair triggering
 */

struct ScrubStats {
    uint64_t chunks_scanned;
    uint64_t chunks_verified;
    uint64_t corrupted_detected;
    uint64_t repairs_triggered;
    uint64_t last_scan_us;
};

class ScrubberService {
public:
    ScrubberService();
    
    /**
     * Start background scrubbing.
     *
     * Scans chunks and verifies:
     * - Checksum validity
     * - Replication factor
     * - Chunk existence on nodes
     */
    void start_scrub();
    
    /**
     * Verify a single chunk integrity.
     *
     * Returns:
     *   true if chunk passes all checks
     */
    bool verify_chunk(uint32_t file_id, uint32_t version_id, uint32_t chunk_index);
    
    /**
     * Check replication factor for a chunk.
     *
     * Returns:
     *   Current number of replicas
     */
    uint32_t check_replication(uint32_t chunk_index);
    
    /**
     * Get scrubbing statistics.
     */
    ScrubStats get_stats() const;
    
    /**
     * Get list of corrupted chunks detected.
     */
    std::vector<uint32_t> get_corrupted_chunks() const;

private:
    ScrubStats stats_;
    std::vector<uint32_t> corrupted_chunks_;
};

}  // namespace filegroup
