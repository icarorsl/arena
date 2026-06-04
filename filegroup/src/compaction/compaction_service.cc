#include "compaction/compaction_service.h"

#include <iostream>
#include <set>
#include <unordered_map>
#include <cstdio>
#include <chrono>
#include <dirent.h>
#include <sys/stat.h>

#include "common/clock.h"
#include "common/types.h"
#include "engine/engine.h"
#include "manifest/file_index.h"
#include "registry/registry_service.h"
#include "segment/segment.h"
#include "storage/storage_node_service.h"

namespace filegroup {

static std::vector<std::string> find_segment_files(const std::string& dir) {
    std::vector<std::string> paths;
    DIR* dp = opendir(dir.c_str());
    if (!dp) return paths;
    struct dirent* de;
    while ((de = readdir(dp)) != nullptr) {
        std::string name(de->d_name);
        if (name.size() > 4 && name.substr(name.size()-4) == ".seg") {
            paths.push_back(dir + "/" + name);
        }
    }
    closedir(dp);
    return paths;
}

CompactionService::CompactionService(Engine& engine, RegistryClient* registry,
                                     const std::vector<StorageClient*>& storage_nodes,
                                     uint32_t interval_seconds)
    : engine_(engine)
    , registry_(registry)
    , storage_nodes_(storage_nodes)
    , interval_seconds_(interval_seconds)
{}

CompactionService::~CompactionService() { stop(); }

void CompactionService::start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread(&CompactionService::run_loop, this);
}

void CompactionService::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void CompactionService::run_loop() {
    std::cerr << "[compaction] Started, interval=" << interval_seconds_ << "s" << std::endl;
    while (running_.load()) {
        // Sleep in 1-second increments so we can respond to stop() quickly
        for (uint32_t i = 0; i < interval_seconds_ && running_.load(); i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!running_.load()) break;

        uint32_t compacted = run_once();
        if (compacted > 0) {
            std::cerr << "[compaction] Compacted " << compacted << " segments" << std::endl;
        }
    }
    std::cerr << "[compaction] Stopped" << std::endl;
}

uint32_t CompactionService::run_once() {
    uint32_t segments_compacted = 0;
    uint32_t total_segments = 0, total_chunks = 0, total_dead = 0;
    std::set<uint64_t> reclaimed_file_ids;  // file_ids whose chunks were removed

    // Build set of live physical file IDs (those with >=1 non-deleted version)
    std::set<uint64_t> live_file_ids;
    // Also build reverse map: physical file_id → LogicalFileEntry (for safety checks)
    std::unordered_map<uint64_t, const LogicalFileEntry*> phys_to_logical;
    auto all_files = registry_->all_files();
    for (const auto& f : all_files) {
        for (const auto& [vn, ver] : f.versions) {
            if (ver.state == VersionState::COMPLETE ||
                ver.state == VersionState::SUPERSEDED ||
                ver.state == VersionState::MARKED_DELETED ||
                ver.state == VersionState::UPLOADING) {
                live_file_ids.insert(ver.file_id);
            }
            if (phys_to_logical.find(ver.file_id) == phys_to_logical.end()) {
                phys_to_logical[ver.file_id] = &f;
            }
        }
    }

    for (auto* sc : storage_nodes_) {
        if (!sc || !sc->ping()) continue;

        auto* server = sc->server();
        // Scan data directory for segment files (not just in-memory open segments)
        auto seg_paths = find_segment_files(server->data_dir());
        total_segments += seg_paths.size();
        if (seg_paths.empty()) continue;

        // Build a set of chunks that should be kept (live)
        // For each segment, read chunk headers; a chunk is live if its version
        // is not DELETED/EXPIRED/SUPERSEDED in the file index.
        //
        // We compact segments individually: read all chunks, filter live ones,
        // write to a fresh segment, then replace.

        for (const auto& seg_path : seg_paths) {
            try {
                Segment old_seg(seg_path);
                uint32_t chunk_count = old_seg.chunk_count();
                total_chunks += chunk_count;
                if (chunk_count == 0) continue;

                // ---- Pass 1: collect live chunk metadata ----
                struct LiveChunk {
                    uint64_t offset;        // header offset in old segment
                    uint64_t file_id;
                    uint32_t chunk_index;
                    uint64_t chunk_size;
                    uint32_t chunk_checksum;
                    bool is_encrypted;
                    uint64_t new_offset;    // filled in Pass 3
                };
                std::vector<LiveChunk> live_chunks;

                uint64_t cursor = sizeof(SegmentFileHeader);
                uint32_t dead_count = 0;

                for (uint32_t i = 0; i < chunk_count; i++) {
                    auto ceh = old_seg.read_chunk_header_at(cursor);
                    uint64_t data_offset = cursor + sizeof(ChunkEntryHeader);
                    uint64_t next_cursor = data_offset + ceh.chunk_size;

                    // A chunk is live if not explicitly deleted AND its file_id has
                    // at least one non-deleted version. We use a conservative check:
                    // if is_deleted=0 and the file_id has no live version tracked,
                    // we KEEP the chunk rather than risk data loss from stale metadata.
                    bool keep = !ceh.is_deleted;
                    if (keep && live_file_ids.count(ceh.file_id) == 0) {
                        // file_id not in live set — check reverse map
                        auto it = phys_to_logical.find(ceh.file_id);
                        if (it != phys_to_logical.end()) {
                            bool has_live = false;
                            for (auto& [vn, ver] : it->second->versions) {
                                if (ver.file_id == ceh.file_id &&
                                    (ver.state == VersionState::COMPLETE ||
                                     ver.state == VersionState::SUPERSEDED ||
                                     ver.state == VersionState::MARKED_DELETED ||
                                     ver.state == VersionState::UPLOADING)) {
                                    has_live = true;
                                    break;
                                }
                            }
                            keep = has_live;
                        }
                        // else: physical file_id not referenced by any logical file → orphaned, delete
                    }

                    if (keep) {
                        live_chunks.push_back({cursor, ceh.file_id,
                                               ceh.chunk_index, ceh.chunk_size,
                                               ceh.chunk_checksum, ceh.is_encrypted != 0});
                    } else {
                        dead_count++;
                        total_dead++;
                        reclaimed_file_ids.insert(ceh.file_id);
                    }

                    cursor = next_cursor;
                }

                // ---- Pass 2: if nothing dead, skip ----
                if (dead_count == 0) continue;

                // ---- Pass 3: write live chunks to new segment ----
                // Write to a temp file, then atomically rename to final path
                std::string tmp_path = seg_path + ".tmp";
                {
                    Segment new_seg(tmp_path, /*create=*/true);
                    for (auto& lc : live_chunks) {
                        auto data = old_seg.read_chunk(lc.offset, lc.chunk_size);
                        if (data.size() != lc.chunk_size) continue;

                        lc.new_offset = new_seg.write_chunk(
                            lc.file_id, lc.chunk_index,
                            data.data(), data.size(),
                            lc.chunk_checksum, lc.is_encrypted);
                    }
                } // close new_seg

                // ---- Pass 4: replace old segment with new ----
                std::string bak_path = seg_path + ".bak";
                ::rename(seg_path.c_str(), bak_path.c_str());
                ::rename(tmp_path.c_str(), seg_path.c_str());
                ::unlink(bak_path.c_str());

                // Update engine chunk locations to final path + new offsets
                for (auto& lc : live_chunks) {
                    engine_.update_chunk_location(lc.file_id, lc.chunk_index,
                                                  seg_path, lc.new_offset, lc.chunk_size);
                }

                // Invalidate cached segment so next write opens the new file
                // Parse group/table from segment filename: page_N_G_T_X.seg or seg_N_G_T_X.seg
                uint32_t seg_group = 0, seg_table = 0;
                size_t last_slash = seg_path.rfind('/');
                std::string fname = (last_slash != std::string::npos) ? seg_path.substr(last_slash + 1) : seg_path;
                sscanf(fname.c_str(), "page_%*u_%u_%u", &seg_group, &seg_table);
                if (seg_group == 0) sscanf(fname.c_str(), "seg_%*u_%u_%u", &seg_group, &seg_table);
                for (auto* sc : storage_nodes_) {
                    if (sc && seg_group > 0 && seg_table > 0)
                        sc->invalidate_segment(seg_group, seg_table);
                }

                segments_compacted++;
                std::cerr << "[compaction] " << seg_path << ": "
                          << dead_count << " dead chunks removed, "
                          << live_chunks.size() << " kept" << std::endl;

            } catch (const std::exception& e) {
                std::cerr << "[compaction] Error processing " << seg_path
                          << ": " << e.what() << "\n";
            }
        }
    }

    if (total_segments > 0 && segments_compacted == 0) {
        std::cerr << "[compaction] Scanned " << total_segments << " segments, "
                  << total_chunks << " chunks, " << total_dead
                  << " dead — nothing to compact" << std::endl;
    }

    // Transition MARKED_DELETED → DELETED for reclaimed file_ids
    if (!reclaimed_file_ids.empty()) {
        auto all_files = registry_->all_files();
        for (const auto& f : all_files) {
            for (const auto& [vn, ver] : f.versions) {
                if (ver.state == VersionState::MARKED_DELETED &&
                    reclaimed_file_ids.count(ver.file_id)) {
                    VersionReclaimedEntry e;
                    e.file_id = ver.file_id;
                    e.logical_file_id = f.logical_file_id;
                    e.version_number = vn;
                    registry_->append_entry((uint32_t)ManifestEntryType::VERSION_RECLAIMED, &e, sizeof(e));
                }
            }
        }
    }

    return segments_compacted;
}

}  // namespace filegroup
