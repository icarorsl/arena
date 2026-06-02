#pragma once

#include <string>
#include "manifest/manifest.h"

namespace filegroup {

// Forward declaration
class FileIndex;

/**
 * Replay manifest log into file index.
 * Reads all entries sequentially, validates CRC32C per entry,
 * applies each to the file index.
 *
 * On CRC32C mismatch: logs the error and skips the entry.
 * On unknown entry type: logs and skips using the length field.
 * Never crashes — always continues replay.
 *
 * @param manifest_path path to manifest file
 * @param index file index to apply entries to
 */
void replay_manifest(const std::string& manifest_path, FileIndex& index);

}  // namespace filegroup
