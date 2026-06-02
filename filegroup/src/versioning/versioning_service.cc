#include "versioning/versioning_service.h"
#include "common/clock.h"
#include <algorithm>
#include <stdexcept>

namespace filegroup {

VersioningService::VersioningService() {}

uint32_t VersioningService::create_version(uint32_t file_id, const VersionConfig& config) {
    if (file_id == 0) {
        throw std::invalid_argument("file_id must not be zero");
    }
    
    auto& file_versions = files_[file_id];
    uint32_t version_id = file_versions.next_version_id++;
    
    VersionInfo info;
    info.version_id = version_id;
    info.state = "open";
    info.created_at_us = now_us();
    info.completed_at_us = 0;
    info.expires_at_us = now_us() + (config.retention_days * 24 * 3600 * 1000000);
    info.file_size_bytes = 0;
    info.chunk_count = 0;
    
    file_versions.versions[version_id] = info;
    
    return version_id;
}

bool VersioningService::complete_version(uint32_t file_id, uint32_t version_id) {
    auto file_it = files_.find(file_id);
    if (file_it == files_.end()) {
        return false;
    }
    
    auto& file_versions = file_it->second;
    auto version_it = file_versions.versions.find(version_id);
    if (version_it == file_versions.versions.end()) {
        return false;
    }
    
    version_it->second.state = "complete";
    version_it->second.completed_at_us = now_us();
    
    return true;
}

VersionInfo VersioningService::get_version(uint32_t file_id, uint32_t version_id) {
    auto file_it = files_.find(file_id);
    if (file_it == files_.end()) {
        throw std::runtime_error("file not found");
    }
    
    auto& file_versions = file_it->second;
    auto version_it = file_versions.versions.find(version_id);
    if (version_it == file_versions.versions.end()) {
        throw std::runtime_error("version not found");
    }
    
    return version_it->second;
}

uint32_t VersioningService::get_latest_complete(uint32_t file_id) {
    auto file_it = files_.find(file_id);
    if (file_it == files_.end()) {
        return 0;
    }
    
    auto& file_versions = file_it->second;
    
    uint32_t latest_id = 0;
    uint64_t latest_time = 0;
    
    for (const auto& [version_id, info] : file_versions.versions) {
        if (info.state == "complete" && info.completed_at_us > latest_time) {
            latest_id = version_id;
            latest_time = info.completed_at_us;
        }
    }
    
    return latest_id;
}

int VersioningService::enforce_max_versions(uint32_t file_id, uint32_t max_versions) {
    auto file_it = files_.find(file_id);
    if (file_it == files_.end()) {
        return 0;
    }
    
    auto& file_versions = file_it->second;
    
    // Count versions
    if (file_versions.versions.size() <= max_versions) {
        return 0;
    }
    
    int deleted = 0;
    
    // Delete oldest versions until under limit
    while (file_versions.versions.size() > max_versions) {
        uint32_t oldest_id = 0;
        uint64_t oldest_time = UINT64_MAX;
        
        for (const auto& [version_id, info] : file_versions.versions) {
            if (info.state != "complete" && info.created_at_us < oldest_time) {
                oldest_id = version_id;
                oldest_time = info.created_at_us;
            }
        }
        
        if (oldest_id == 0) {
            break;  // No non-complete versions to delete
        }
        
        file_versions.versions.erase(oldest_id);
        deleted++;
    }
    
    return deleted;
}

bool VersioningService::expire_version(uint32_t file_id, uint32_t version_id) {
    auto file_it = files_.find(file_id);
    if (file_it == files_.end()) {
        return false;
    }
    
    auto& file_versions = file_it->second;
    auto version_it = file_versions.versions.find(version_id);
    if (version_it == file_versions.versions.end()) {
        return false;
    }
    
    version_it->second.state = "expired";
    
    return true;
}

std::vector<VersionInfo> VersioningService::list_versions(uint32_t file_id) {
    std::vector<VersionInfo> result;
    
    auto file_it = files_.find(file_id);
    if (file_it == files_.end()) {
        return result;
    }
    
    auto& file_versions = file_it->second;
    
    for (const auto& [version_id, info] : file_versions.versions) {
        result.push_back(info);
    }
    
    return result;
}

}  // namespace filegroup
