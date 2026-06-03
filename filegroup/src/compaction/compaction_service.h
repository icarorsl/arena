#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace filegroup {

class Engine;
class RegistryClient;
class StorageClient;

/**
 * CompactionService — Background thread that periodically compacts segment files.
 *
 * Phase 1: runs every N minutes, iterates all storage nodes, reads chunks
 * from each segment, copies live chunks to new compacted segments, and
 * updates the engine's chunk location map.
 *
 * "Live" means the chunk's version is COMPLETE or SUPERSEDED (not DELETED/EXPIRED).
 */
class CompactionService {
public:
    /// @param engine      Engine instance (to update chunk locations after compaction)
    /// @param registry    Registry client (to query version state)
    /// @param storage_nodes All storage clients (to access segment files)
    /// @param interval_seconds How often to run compaction (default: 3600 = 1 hour)
    CompactionService(Engine& engine, RegistryClient* registry,
                      const std::vector<StorageClient*>& storage_nodes,
                      uint32_t interval_seconds = 3600);

    ~CompactionService();

    /// Start the background compaction thread.
    void start();

    /// Stop the background thread.
    void stop();

    /// Trigger a single compaction pass immediately (blocking).
    /// Returns the number of segments compacted.
    uint32_t run_once();

    /// Whether the service is running.
    bool running() const { return running_.load(); }

private:
    void run_loop();

    Engine& engine_;
    RegistryClient* registry_;
    std::vector<StorageClient*> storage_nodes_;
    uint32_t interval_seconds_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace filegroup
