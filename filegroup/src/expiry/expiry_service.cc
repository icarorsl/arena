#include "expiry/expiry_service.h"

#include <chrono>
#include <iostream>

#include "common/clock.h"
#include "common/types.h"
#include "manifest/file_index.h"
#include "manifest/manifest.h"
#include "registry/registry_service.h"

namespace filegroup {

ExpiryService::ExpiryService(RegistryClient* registry, uint64_t interval_sec)
    : registry_(registry)
    , interval_us_(interval_sec * 1'000'000)
{
}

ExpiryService::~ExpiryService() {
    stop();
}

void ExpiryService::start() {
    running_ = true;
    thread_ = std::thread(&ExpiryService::run, this);
}

void ExpiryService::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void ExpiryService::run() {
    while (running_) {
        scan();

        // Sleep in chunks so we can stop quickly
        for (uint64_t i = 0; i < interval_us_ && running_; i += 1'000'000) {
            std::this_thread::sleep_for(std::chrono::microseconds(1'000'000));
        }
    }
}

void ExpiryService::scan() {
    uint64_t now = now_us();
    uint64_t expired_count = 0;

    // Iterate all tables from the registry
    auto tables = registry_->get_tables();

    for (auto& table : tables) {
        auto files = registry_->list_files((uint16_t)table.table_id, table.group_id);

        for (auto& file : files) {
            for (auto& [vn, ver] : file.versions) {
                // Only expire COMPLETE/SUPERSEDED versions with non-zero expiry
                if (ver.expires_at == 0) continue;
                if (ver.state != VersionState::COMPLETE &&
                    ver.state != VersionState::SUPERSEDED) continue;
                if (now < ver.expires_at) continue;

                // Mark version as expired
                VersionDeletedEntry e;
                e.file_id = ver.file_id;
                e.logical_file_id = file.logical_file_id;
                e.version_number = ver.version_number;

                auto [ok, lsn] = registry_->append_entry(
                    (uint32_t)ManifestEntryType::VERSION_DELETED, &e, sizeof(e));

                if (ok) {
                    expired_count++;
                    std::cout << "[expiry] Expired v" << ver.version_number
                              << " of file " << file.logical_file_id
                              << " (table " << table.table_id << ")\n";
                }
            }
        }
    }

    if (expired_count > 0) {
        std::cout << "[expiry] Scan complete: " << expired_count
                  << " versions expired\n";
    }

    std::lock_guard<std::mutex> lk(stats_mutex_);
    versions_expired_ += expired_count;
    last_scan_us_ = now;
}

}  // namespace filegroup
