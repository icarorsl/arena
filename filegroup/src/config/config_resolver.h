#pragma once

#include "config/config.h"
#include "common/types.h"

namespace filegroup {

/**
 * Resolve chunk size using three-level priority chain:
 * 1. File/table override (if non-zero)
 * 2. Table default (if non-zero)
 * 3. Group default
 */
uint64_t resolve_chunk_size(
    const FileGroupConfig& group,
    const FileTableConfig* table = nullptr,
    uint64_t file_override = 0);

/**
 * Resolve replication factor using three-level priority chain:
 * 1. File/table override (if non-zero)
 * 2. Table default (if non-zero)
 * 3. Group default
 */
uint8_t resolve_replication_factor(
    const FileGroupConfig& group,
    const FileTableConfig* table = nullptr,
    uint8_t file_override = 0);

/**
 * Resolve expiry timestamp from days.
 * Priority: file_expires_in_days (table or implicit) → table → group default
 * Converts days to Unix microseconds from now_us().
 *
 * @param now_us current Unix microseconds
 * @param group file group config
 * @param table file table config (optional)
 * @param file_expires_in_days days from now (0 = no expiry, use default)
 * @return Unix microseconds (0 if no expiry), or computed expires_at timestamp
 */
uint64_t resolve_expires_at(
    const FileGroupConfig& group,
    const FileTableConfig* table,
    uint32_t file_expires_in_days,
    uint64_t now_us);

/**
 * Resolve expiry granularity using three-level priority chain:
 * 1. File/table override (if not UNSET)
 * 2. Table default (if not UNSET)
 * 3. Group default
 */
ExpiryGranularity resolve_expiry_granularity(
    const FileGroupConfig& group,
    const FileTableConfig* table = nullptr,
    ExpiryGranularity file_override = ExpiryGranularity::UNSET);

/**
 * Resolve encryption algorithm.
 * Table setting overrides group setting (if not NONE).
 * If file explicitly specifies an algo, that takes precedence.
 */
EncryptionAlgo resolve_encryption(
    const FileGroupConfig& group,
    const FileTableConfig* table = nullptr,
    EncryptionAlgo file_override = EncryptionAlgo::NONE);

/**
 * Resolve encryption key file path.
 * Priority: table key → group key
 * Returns empty string if no encryption key configured.
 */
std::string resolve_encryption_key_file(
    const FileGroupConfig& group,
    const FileTableConfig* table = nullptr);

/**
 * Resolve max_versions.
 * Priority: table override (if non-zero) → group default (if non-zero) → 0 (unlimited)
 */
uint32_t resolve_max_versions(
    const FileGroupConfig& group,
    const FileTableConfig* table = nullptr);

}  // namespace filegroup
