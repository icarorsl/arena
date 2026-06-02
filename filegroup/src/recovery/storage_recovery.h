#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace filegroup {

/**
 * Storage Node Recovery Service (Phase 1)
 *
 * Handles recovery of storage nodes after failures:
 * 1. Detect dead or degraded nodes
 * 2. Trigger chunk re-replication
 * 3. Rebuild lost replicas
 * 4. Monitor replication progress
 */

struct RecoveryTaskStatus {
    uint32_t chunk_index;
    uint32_t source_node;
    uint32_t target_node;
    std::string status;  // "pending", "in_progress", "complete", "failed"
};

struct StorageRecoveryStats {
    uint64_t chunks_to_recover;
    uint64_t chunks_recovered;
    uint64_t bytes_recovered;
    uint64_t failed_recoveries;
};

class StorageRecoveryService {
public:
    StorageRecoveryService();
    
    /**
     * Start recovery for a dead node.
     *
     * Detects all chunks that were on the dead node
     * and queues them for re-replication from replicas.
     *
     * Returns:
     *   Number of recovery tasks queued
     */
    uint64_t recover_node(uint32_t dead_node_id);
    
    /**
     * Get recovery task status.
     */
    RecoveryTaskStatus get_task_status(uint32_t chunk_index);
    
    /**
     * Get recovery statistics.
     */
    StorageRecoveryStats get_stats() const;
    
    /**
     * Process one recovery task.
     *
     * Returns:
     *   true if task completed successfully
     */
    bool process_recovery_task(uint32_t chunk_index);

private:
    std::map<uint32_t, RecoveryTaskStatus> recovery_tasks_;
    StorageRecoveryStats stats_;
};

}  // namespace filegroup
