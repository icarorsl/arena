#include "compaction/compaction_service.h"

#include <iostream>
#include <set>
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

    // Build set of live physical file IDs (those with >=1 non-deleted version)
    std::set<uint64_t> live_file_ids;
    auto all_files = registry_->all_files();
    for (const auto& f : all_files) {
        for (const auto& [vn, ver] : f.versions) {
            if (ver.state == VersionState::COMPLETE ||
                ver.state == VersionState::SUPERSEDED) {
                live_file_ids.insert(ver.file_id);
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
                    uint64_t offset;
                    uint64_t file_id;
                    uint32_t chunk_index;
                    uint64_t chunk_size;
                    uint32_t chunk_checksum;
                    bool is_encrypted;
                };
                std::vector<LiveChunk> live_chunks;

                uint64_t cursor = sizeof(SegmentFileHeader);
                uint32_t dead_count = 0;

                for (uint32_t i = 0; i < chunk_count; i++) {
                    auto ceh = old_seg.read_chunk_header_at(cursor);
                    uint64_t data_offset = cursor + sizeof(ChunkEntryHeader);
                    uint64_t next_cursor = data_offset + ceh.chunk_size;
                    bool live = !ceh.is_deleted && live_file_ids.count(ceh.file_id) > 0;

                    if (live) {
                        live_chunks.push_back({cursor, ceh.file_id,
                                               ceh.chunk_index, ceh.chunk_size,
                                               ceh.chunk_checksum, ceh.is_encrypted != 0});
                    } else {
                        dead_count++;
                        total_dead++;
                    }

                    cursor = next_cursor;
                }

                // ---- Pass 2: if nothing dead, skip ----
                if (dead_count == 0) continue;

                // ---- Pass 3: write live chunks to new segment ----
                std::string new_path = seg_path + ".compact";
                Segment new_seg(new_path, /*create=*/true);

                for (auto& lc : live_chunks) {
                    auto data = old_seg.read_chunk(lc.offset, lc.chunk_size);
                    if (data.size() != lc.chunk_size) continue;

                    uint64_t new_offset = new_seg.write_chunk(
                        lc.file_id, lc.chunk_index,
                        data.data(), data.size(),
                        lc.chunk_checksum, lc.is_encrypted);

                    // Update engine's chunk location
                    engine_.update_chunk_location(lc.file_id, lc.chunk_index,
                                                  new_path, new_offset, lc.chunk_size);
                }

                // ---- Pass 4: replace old segment with new ----
                // Close both segments (destructors handle this)
                // Rename new over old
                std::string bak_path = seg_path + ".bak";
                ::rename(seg_path.c_str(), bak_path.c_str());
                ::rename(new_path.c_str(), seg_path.c_str());
                ::unlink(bak_path.c_str());

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
    return segments_compacted;
}

}  // namespace filegroup
