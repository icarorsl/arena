#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace filegroup {

/**
 * Engine Recovery Service (Phase 1)
 *
 * Recovers engine state from manifest log on restart:
 * 1. Replay manifest entries
 * 2. Rebuild file index from log
 * 3. Verify chunk locations
 * 4. Rebuild version info
 */

struct RecoveryStats {
    uint64_t entries_replayed;
    uint64_t files_recovered;
    uint64_t versions_recovered;
    uint64_t chunks_verified;
    uint64_t recovery_time_us;
};

class EngineRecoveryService {
public:
    EngineRecoveryService();
    
    /**
     * Perform engine recovery from manifest log.
     *
     * Steps:
     * 1. Open manifest log
     * 2. Iterate through all entries
     * 3. Apply each entry to rebuild index
     * 4. Verify consistency
     *
     * Returns:
     *   RecoveryStats with details
     *
     * Throws:
     *   std::runtime_error on critical recovery failures
     */
    RecoveryStats recover_from_manifest(const std::string& manifest_path);
    
    /**
     * Verify chunk locations after recovery.
     *
     * Returns:
     *   Number of chunks verified
     */
    uint64_t verify_chunk_locations();
    
    /**
     * Check if recovery is needed (no index file or corrupted).
     *
     * Returns:
     *   true if recovery should be run
     */
    bool is_recovery_needed(const std::string& manifest_path);
    
    /**
     * Get last recovery stats.
     */
    RecoveryStats get_last_recovery_stats() const;

private:
    RecoveryStats last_recovery_;
};

}  // namespace filegroup
