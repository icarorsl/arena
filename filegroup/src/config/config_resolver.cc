#include "config/config_resolver.h"

#include "common/clock.h"

namespace filegroup {

uint64_t resolve_chunk_size(
    const FileGroupConfig& group,
    const FileTableConfig* table,
    uint64_t file_override) {
    if (file_override > 0) return file_override;
    if (table && table->chunk_size > 0) return table->chunk_size;
    return group.chunk_size;
}

uint8_t resolve_replication_factor(
    const FileGroupConfig& group,
    const FileTableConfig* table,
    uint8_t file_override) {
    if (file_override > 0) return file_override;
    if (table && table->replication_factor > 0) return table->replication_factor;
    return group.replication_factor;
}

uint64_t resolve_expires_at(
    const FileGroupConfig& group,
    const FileTableConfig* table,
    uint32_t file_expires_in_days,
    uint64_t now_us) {
    // Determine effective expiry days
    uint32_t expires_in_days = 0;

    if (file_expires_in_days > 0) {
        // File explicitly specifies expiry
        expires_in_days = file_expires_in_days;
    } else if (table && table->file_expires_in_days > 0) {
        // Table has a default
        expires_in_days = table->file_expires_in_days;
    } else if (group.file_expires_in_days > 0) {
        // Group has a default
        expires_in_days = group.file_expires_in_days;
    } else {
        // No expiry
        return 0;
    }

    // Convert days to microseconds
    uint64_t expires_in_us = static_cast<uint64_t>(expires_in_days) * 24 * 60 * 60 * 1000000ULL;
    return now_us + expires_in_us;
}

ExpiryGranularity resolve_expiry_granularity(
    const FileGroupConfig& group,
    const FileTableConfig* table,
    ExpiryGranularity file_override) {
    if (file_override != ExpiryGranularity::UNSET) return file_override;
    if (table && table->expiry_granularity != ExpiryGranularity::UNSET) {
        return table->expiry_granularity;
    }
    return group.expiry_granularity;
}

EncryptionAlgo resolve_encryption(
    const FileGroupConfig& group,
    const FileTableConfig* table,
    EncryptionAlgo file_override) {
    // File override takes precedence
    if (file_override != EncryptionAlgo::NONE) return file_override;

    // Table override takes precedence over group
    if (table && table->encryption != EncryptionAlgo::NONE) {
        return table->encryption;
    }

    // Group default
    return group.encryption;
}

std::string resolve_encryption_key_file(
    const FileGroupConfig& group,
    const FileTableConfig* table) {
    // Table key overrides group key
    if (table && table->encryption_key) {
        return table->encryption_key->file;
    }
    if (group.encryption_key) {
        return group.encryption_key->file;
    }
    return "";
}

uint32_t resolve_max_versions(
    const FileGroupConfig& group,
    const FileTableConfig* table) {
    // Table override takes precedence
    if (table && table->max_versions > 0) return table->max_versions;

    // Group default
    if (group.max_versions > 0) return group.max_versions;

    // No limit
    return 0;
}

}  // namespace filegroup
