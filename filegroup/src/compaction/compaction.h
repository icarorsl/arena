#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace filegroup {

/**
 * Segment compaction for Phase 1.
 *
 * Merges multiple small segment files into fewer large ones.
 * Removes duplicates and unused chunks.
 */

struct CompactionStats {
    uint32_t input_segments;      // Number of segments before compaction
    uint32_t output_segments;     // Number of segments after compaction
    uint64_t input_size_bytes;    // Total input size
    uint64_t output_size_bytes;   // Total output size
    uint32_t total_chunks_input;  // Chunks before compaction
    uint32_t total_chunks_output; // Chunks after compaction (duplicates removed)
    uint32_t space_saved_bytes;   // Bytes freed by compaction
};

/**
 * Compacts segments by merging and deduplication.
 *
 * Algorithm:
 * - Read all chunks from input segments
 * - Remove duplicate chunks (same chunk_index from same upload session)
 * - Group chunks into new segments by target_segment_size
 * - Write compacted segments to output directory
 *
 * Parameters:
 *   input_segments: Vector of input segment file paths
 *   output_directory: Directory to write compacted segments
 *   target_segment_size: Target size for each output segment (bytes)
 *   max_chunks_per_segment: Maximum chunks per output segment
 *
 * Returns:
 *   CompactionStats with input/output metrics
 *
 * Throws:
 *   std::runtime_error if I/O operations fail
 *   std::invalid_argument if parameters invalid
 */
CompactionStats compact_segments(
    const std::vector<std::string>& input_segments,
    const std::string& output_directory,
    uint64_t target_segment_size = 1024 * 1024 * 1024,  // 1 GB default
    uint32_t max_chunks_per_segment = 10000
);

/**
 * Calculates deduplication statistics across segments.
 *
 * Reads all segments and counts duplicate chunks without compacting.
 *
 * Parameters:
 *   segments: Vector of segment file paths to analyze
 *
 * Returns:
 *   CompactionStats with duplicate counts (input_size = total, output_size = after dedup)
 */
CompactionStats analyze_deduplication(
    const std::vector<std::string>& segments
);

/**
 * Estimates compaction savings without actually compacting.
 *
 * Returns:
 *   Estimated space that could be saved
 */
uint64_t estimate_compaction_savings(
    const std::vector<std::string>& segments
);

/**
 * Validates that all chunks in a segment file are unique.
 *
 * Checks:
 * - No duplicate chunk_index values within same segment
 * - All chunk headers are valid
 * - Checksums match actual data
 */
bool validate_segment_uniqueness(
    const std::string& segment_path
);

}  // namespace filegroup
