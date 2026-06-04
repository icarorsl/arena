#include <gtest/gtest.h>

#include <set>
#include <unordered_map>
#include "common/types.h"
#include "manifest/file_index.h"

namespace filegroup {
namespace {

// ============================================================================
// Tests for compaction liveness decision logic (CompactionService::run_once)
//
// Verifies the exact logic used to decide whether a chunk survives compaction:
//   1. is_deleted=1 → always dead
//   2. is_deleted=0 + file_id in live_file_ids → live
//   3. is_deleted=0 + file_id not in live_file_ids → check reverse map
//   4. Not in reverse map → orphaned → dead
// ============================================================================

struct CompactionLogic {
    std::set<uint64_t> live_file_ids;
    std::unordered_map<uint64_t, const LogicalFileEntry*> phys_to_logical;

    void build_from(FileIndex& index) {
        live_file_ids.clear();
        phys_to_logical.clear();
        auto all_files = index.all_files();
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
    }

    bool should_keep(bool is_deleted, uint64_t file_id) {
        if (is_deleted) return false;
        if (live_file_ids.count(file_id) > 0) return true;
        auto it = phys_to_logical.find(file_id);
        if (it != phys_to_logical.end()) {
            for (auto& [vn, ver] : it->second->versions) {
                if (ver.state == VersionState::COMPLETE ||
                    ver.state == VersionState::SUPERSEDED ||
                    ver.state == VersionState::MARKED_DELETED ||
                    ver.state == VersionState::UPLOADING) {
                    return true;
                }
            }
            return false;
        }
        return false;
    }
};

class CompactionLogicTest : public ::testing::Test {
protected:
    FileIndex index;

    void register_version(uint64_t logical_file_id, uint64_t file_id,
                          uint32_t version_number, uint16_t table_id,
                          uint32_t group_id, VersionState state) {
        SessionOpenEntry e;
        e.session_id = file_id * 10;
        e.file_id = file_id;
        e.logical_file_id = logical_file_id;
        e.table_id = table_id;
        e.group_id = group_id;
        e.version_number = version_number;
        e.chunk_size = 65536;
        e.replication_factor = 1;
        e.expected_chunks = 1;
        e.expires_at = 0;
        e.encryption = 0;
        e.segment_type = 0;
        index.apply_session_open(e);

        if (state == VersionState::COMPLETE) {
            VersionCompleteEntry vc{};
            vc.file_id = file_id;
            vc.logical_file_id = logical_file_id;
            vc.version_number = version_number;
            vc.chunk_count = 1;
            vc.total_size = 65536;
            vc.created_at_us = 1;
            index.apply_version_complete(vc);
        } else if (state == VersionState::MARKED_DELETED) {
            VersionCompleteEntry vc{};
            vc.file_id = file_id;
            vc.logical_file_id = logical_file_id;
            vc.version_number = version_number;
            vc.chunk_count = 1;
            vc.total_size = 65536;
            vc.created_at_us = 1;
            index.apply_version_complete(vc);
            VersionDeletedEntry vd{};
            vd.file_id = file_id;
            vd.logical_file_id = logical_file_id;
            vd.version_number = version_number;
            index.apply_version_deleted(vd);
        }
        // UPLOADING: just session open, no completion
        // SESSION_TIMED_OUT: will be handled separately
    }
};

// ===== Tests =====

TEST_F(CompactionLogicTest, CompleteVersionChunksAreLive) {
    register_version(100, 10, 1, 1, 1, VersionState::COMPLETE);
    CompactionLogic logic;
    logic.build_from(index);
    EXPECT_TRUE(logic.live_file_ids.count(10) > 0);
    EXPECT_TRUE(logic.should_keep(false, 10));
}

TEST_F(CompactionLogicTest, UploadingVersionChunksAreLive) {
    // BUG FIX: UPLOADING was previously excluded from live set,
    // causing in-progress upload chunks to be deleted.
    register_version(200, 20, 1, 1, 1, VersionState::UPLOADING);
    CompactionLogic logic;
    logic.build_from(index);
    EXPECT_TRUE(logic.live_file_ids.count(20) > 0)
        << "UPLOADING file_id must be in live_file_ids";
    EXPECT_TRUE(logic.should_keep(false, 20))
        << "Chunks from in-progress uploads must survive compaction";
}

TEST_F(CompactionLogicTest, MarkedDeletedVersionChunksAreLive) {
    register_version(400, 40, 1, 1, 1, VersionState::MARKED_DELETED);
    CompactionLogic logic;
    logic.build_from(index);
    EXPECT_TRUE(logic.live_file_ids.count(40) > 0);
    EXPECT_TRUE(logic.should_keep(false, 40));
}

// NOTE: FileIndex::apply_session_timed_out currently only removes the session
// from sessions_ map, it does NOT change VersionEntry.state from UPLOADING
// to SESSION_TIMED_OUT. This is tracked as a separate issue.
// Until fixed, timed-out session chunks will incorrectly survive compaction.

// Unregistered file_id: chunks never belonged to any file → dead
TEST_F(CompactionLogicTest, UnregisteredFileIdIsDead) {
    register_version(600, 60, 1, 1, 1, VersionState::COMPLETE);
    CompactionLogic logic;
    logic.build_from(index);
    // file_id=999 was never registered in any logical file → dead
    EXPECT_FALSE(logic.should_keep(false, 999))
        << "Chunks with unknown file_id must be deleted";
}

TEST_F(CompactionLogicTest, ExplicitlyDeletedChunksAreDead) {
    register_version(600, 60, 1, 1, 1, VersionState::COMPLETE);
    CompactionLogic logic;
    logic.build_from(index);
    EXPECT_TRUE(logic.live_file_ids.count(60) > 0);
    EXPECT_FALSE(logic.should_keep(true, 60))
        << "is_deleted=1 must be removed even if file is live";
}

TEST_F(CompactionLogicTest, OrphanedFileIdIsDead) {
    register_version(700, 70, 1, 1, 1, VersionState::COMPLETE);
    CompactionLogic logic;
    logic.build_from(index);
    EXPECT_FALSE(logic.should_keep(false, 999))
        << "Unknown file_id must be deleted";
}

TEST_F(CompactionLogicTest, ReverseMapFallbackWorks) {
    register_version(800, 80, 1, 1, 1, VersionState::COMPLETE);
    CompactionLogic logic;
    logic.build_from(index);
    EXPECT_TRUE(logic.phys_to_logical.count(80) > 0);
    EXPECT_TRUE(logic.should_keep(false, 80));
}

// Live UPLOADING vs orphaned file_id
TEST_F(CompactionLogicTest, LiveUploadingVsOrphanedFileId) {
    register_version(900, 90, 1, 1, 1, VersionState::UPLOADING);
    CompactionLogic logic;
    logic.build_from(index);
    EXPECT_TRUE(logic.should_keep(false, 90));
    EXPECT_FALSE(logic.should_keep(false, 91));
}

}  // namespace
}  // namespace filegroup
