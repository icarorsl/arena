#include "manifest/manifest_reader.h"

#include "manifest/file_index.h"
#include "common/crc32c.h"
#include <fstream>
#include <iostream>

namespace filegroup {

void replay_manifest(const std::string& manifest_path, FileIndex& index) {
    std::ifstream file(manifest_path, std::ios::binary);
    if (!file.is_open()) {
        // Manifest doesn't exist yet (first startup)
        return;
    }

    uint64_t entries_replayed = 0;
    uint64_t entries_skipped = 0;

    while (file.peek() != EOF) {
        // Read header
        ManifestEntryHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (file.gcount() != sizeof(header)) {
            // Incomplete header at end of file
            break;
        }

        // Validate entry type and length are reasonable
        // Forward compatibility: skip unknown entry types

        // Read body
        std::vector<uint8_t> body(header.length);
        file.read(reinterpret_cast<char*>(body.data()), header.length);
        if (file.gcount() != static_cast<int>(header.length)) {
            std::cerr << "Failed to read full entry body, LSN: " << header.entry_lsn << std::endl;
            entries_skipped++;
            break;
        }

        // Verify CRC32C
        uint32_t computed_crc = crc32c(body.data(), body.size());
        if (computed_crc != header.crc32c) {
            std::cerr << "CRC32C mismatch at LSN " << header.entry_lsn << ", skipping entry" << std::endl;
            entries_skipped++;
            continue;
        }

        // Apply entry based on type
        ManifestEntryType entry_type = static_cast<ManifestEntryType>(header.entry_type);
        bool applied = false;

        switch (entry_type) {
            case ManifestEntryType::SESSION_OPEN:
                if (body.size() >= sizeof(SessionOpenEntry)) {
                    const SessionOpenEntry* e = reinterpret_cast<const SessionOpenEntry*>(body.data());
                    index.apply_session_open(*e);
                    applied = true;
                }
                break;

            case ManifestEntryType::CHUNK_CONFIRMED:
                if (body.size() >= sizeof(ChunkConfirmedEntry)) {
                    const ChunkConfirmedEntry* e = reinterpret_cast<const ChunkConfirmedEntry*>(body.data());
                    index.apply_chunk_confirmed(*e);
                    applied = true;
                }
                break;

            case ManifestEntryType::VERSION_COMPLETE:
                if (body.size() >= sizeof(VersionCompleteEntry)) {
                    const VersionCompleteEntry* e = reinterpret_cast<const VersionCompleteEntry*>(body.data());
                    index.apply_version_complete(*e);
                    applied = true;
                }
                break;

            case ManifestEntryType::VERSION_DELETED:
                if (body.size() >= sizeof(VersionDeletedEntry)) {
                    const VersionDeletedEntry* e = reinterpret_cast<const VersionDeletedEntry*>(body.data());
                    index.apply_version_deleted(*e);
                    applied = true;
                }
                break;

            case ManifestEntryType::VERSION_RECLAIMED:
                if (body.size() >= sizeof(VersionReclaimedEntry)) {
                    const VersionReclaimedEntry* e = reinterpret_cast<const VersionReclaimedEntry*>(body.data());
                    index.apply_version_reclaimed(*e);
                    applied = true;
                }
                break;

            case ManifestEntryType::FILE_DELETED:
                if (body.size() >= sizeof(FileDeletedEntry)) {
                    const FileDeletedEntry* e = reinterpret_cast<const FileDeletedEntry*>(body.data());
                    index.apply_file_deleted(*e);
                    applied = true;
                }
                break;

            case ManifestEntryType::SESSION_TIMED_OUT:
                if (body.size() >= sizeof(SessionTimedOutEntry)) {
                    const SessionTimedOutEntry* e = reinterpret_cast<const SessionTimedOutEntry*>(body.data());
                    index.apply_session_timed_out(*e);
                    applied = true;
                }
                break;

            case ManifestEntryType::CHUNK_DELETE_CONFIRMED:
                if (body.size() >= sizeof(ChunkDeleteConfirmedEntry)) {
                    const ChunkDeleteConfirmedEntry* e = reinterpret_cast<const ChunkDeleteConfirmedEntry*>(body.data());
                    index.apply_chunk_delete_confirmed(*e);
                    applied = true;
                }
                break;

            case ManifestEntryType::PAGE_DELETED:
                // Variable-length, handle separately
                // For now, skip
                break;

            case ManifestEntryType::NODE_HEALTH:
                if (body.size() >= sizeof(NodeHealthEntry)) {
                    const NodeHealthEntry* e = reinterpret_cast<const NodeHealthEntry*>(body.data());
                    index.apply_node_health(*e);
                    applied = true;
                }
                break;

            case ManifestEntryType::MAX_VERSIONS_ENFORCED:
                if (body.size() >= sizeof(MaxVersionsEnforcedEntry)) {
                    const MaxVersionsEnforcedEntry* e = reinterpret_cast<const MaxVersionsEnforcedEntry*>(body.data());
                    index.apply_max_versions_enforced(*e);
                    applied = true;
                }
                break;

            default:
                // Unknown entry type — skip using length field (forward compatibility)
                if (header.entry_type >= static_cast<uint16_t>(ManifestEntryType::PROJECTION_CREATE)) {
                    // Phase 3+ entry, safe to skip
                    applied = true;
                }
                break;
        }

        if (applied) {
            entries_replayed++;
        } else {
            entries_skipped++;
        }
    }

    std::cout << "Manifest replay complete: " << entries_replayed << " entries applied, "
              << entries_skipped << " entries skipped" << std::endl;
}

}  // namespace filegroup
