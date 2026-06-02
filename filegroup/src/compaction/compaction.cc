#include "compaction/compaction.h"
#include "segment/segment.h"
#include "segment/segment_reader.h"
#include "segment/segment_writer.h"
#include <fstream>
#include <map>
#include <set>
#include <algorithm>
#include <filesystem>

namespace filegroup {

namespace fs = std::filesystem;

// Key for deduplicating chunks: (session_id, chunk_index)
struct ChunkKey {
    uint32_t session_id;
    uint32_t chunk_index;
    
    bool operator<(const ChunkKey& other) const {
        if (session_id != other.session_id) return session_id < other.session_id;
        return chunk_index < other.chunk_index;
    }
};

struct ChunkData {
    std::vector<uint8_t> data;
    uint32_t checksum;
    uint8_t replication_factor;
    uint32_t chunk_index;
};

CompactionStats compact_segments(
    const std::vector<std::string>& input_segments,
    const std::string& output_directory,
    uint64_t target_segment_size,
    uint32_t max_chunks_per_segment) {
    
    if (input_segments.empty()) {
        throw std::invalid_argument("input_segments is empty");
    }
    
    if (target_segment_size == 0) {
        throw std::invalid_argument("target_segment_size must be > 0");
    }
    
    if (max_chunks_per_segment == 0) {
        throw std::invalid_argument("max_chunks_per_segment must be > 0");
    }
    
    // Ensure output directory exists
    try {
        fs::create_directories(output_directory);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to create output directory: ") + e.what());
    }
    
    CompactionStats stats = {};
    stats.input_segments = input_segments.size();
    
    // Read and deduplicate chunks
    std::map<ChunkKey, ChunkData> unique_chunks;
    uint64_t total_input_size = 0;
    uint32_t duplicate_count = 0;
    
    for (const auto& segment_path : input_segments) {
        // Get input file size
        try {
            total_input_size += fs::file_size(segment_path);
        } catch (...) {
            // Continue if can't get size
        }
        
        try {
            SegmentReader reader(segment_path);
            
            for (const auto& chunk : reader.chunks()) {
                // Create dedup key
                ChunkKey key{0, chunk.chunk_index};
                
                if (unique_chunks.find(key) != unique_chunks.end()) {
                    duplicate_count++;
                } else {
                    ChunkData chunk_data;
                    chunk_data.data = chunk.data;
                    chunk_data.checksum = chunk.checksum;
                    chunk_data.replication_factor = chunk.replication_factor;
                    chunk_data.chunk_index = chunk.chunk_index;
                    unique_chunks[key] = chunk_data;
                }
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("Failed to read segment ") + segment_path + ": " + e.what()
            );
        }
    }
    
    stats.total_chunks_input = unique_chunks.size() + duplicate_count;
    stats.total_chunks_output = unique_chunks.size();
    stats.input_size_bytes = total_input_size;
    
    // Group chunks into new segments
    std::vector<std::vector<ChunkData>> output_segments;
    std::vector<ChunkData> current_segment;
    uint64_t current_size = 0;
    
    for (const auto& [key, chunk] : unique_chunks) {
        uint64_t chunk_total_size = sizeof(ChunkEntry) + chunk.data.size();
        
        // Start new segment if current would exceed target size or chunk limit
        if (!current_segment.empty() &&
            (current_size + chunk_total_size > target_segment_size ||
             current_segment.size() >= max_chunks_per_segment)) {
            
            output_segments.push_back(current_segment);
            current_segment.clear();
            current_size = 0;
        }
        
        // Add chunk to current segment
        current_segment.push_back(chunk);
        current_size += chunk_total_size;
    }
    
    // Add final segment if not empty
    if (!current_segment.empty()) {
        output_segments.push_back(current_segment);
    }
    
    // Write compacted segments
    stats.output_segments = 0;
    stats.output_size_bytes = 0;
    
    for (size_t i = 0; i < output_segments.size(); ++i) {
        std::string output_path = output_directory + "/compacted_" + std::to_string(i) + ".seg";
        
        try {
            SegmentWriter writer(output_path, SegmentType::STANDARD);
            
            // Write chunk data to segment
            for (const auto& chunk_data : output_segments[i]) {
                writer.append_chunk(
                    chunk_data.chunk_index,
                    chunk_data.data.data(),
                    chunk_data.data.size(),
                    chunk_data.replication_factor
                );
            }
            
            writer.finalize();
            
            // Get output file size
            try {
                stats.output_size_bytes += fs::file_size(output_path);
            } catch (...) {}
            
            stats.output_segments++;
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("Failed to write compacted segment ") + output_path + ": " + e.what()
            );
        }
    }
    
    stats.space_saved_bytes = (stats.input_size_bytes > stats.output_size_bytes) ?
                               stats.input_size_bytes - stats.output_size_bytes : 0;
    
    return stats;
}

CompactionStats analyze_deduplication(
    const std::vector<std::string>& segments) {
    
    CompactionStats stats = {};
    stats.input_segments = segments.size();
    
    std::map<ChunkKey, bool> seen_chunks;
    uint32_t total_chunks = 0;
    uint32_t unique_chunks = 0;
    uint64_t total_size = 0;
    
    for (const auto& segment_path : segments) {
        try {
            total_size += fs::file_size(segment_path);
        } catch (...) {}
        
        try {
            SegmentReader reader(segment_path);
            
            for (const auto& chunk : reader.chunks()) {
                total_chunks++;
                
                ChunkKey key{0, chunk.chunk_index};
                if (seen_chunks.find(key) == seen_chunks.end()) {
                    seen_chunks[key] = true;
                    unique_chunks++;
                }
            }
        } catch (...) {}
    }
    
    stats.total_chunks_input = total_chunks;
    stats.total_chunks_output = unique_chunks;
    stats.input_size_bytes = total_size;
    stats.output_size_bytes = total_size * unique_chunks / std::max(1u, total_chunks);
    stats.space_saved_bytes = (stats.input_size_bytes > stats.output_size_bytes) ?
                               stats.input_size_bytes - stats.output_size_bytes : 0;
    
    return stats;
}

uint64_t estimate_compaction_savings(
    const std::vector<std::string>& segments) {
    
    auto stats = analyze_deduplication(segments);
    return stats.space_saved_bytes;
}

bool validate_segment_uniqueness(
    const std::string& segment_path) {
    
    try {
        SegmentReader reader(segment_path);
        std::set<uint32_t> seen_indices;
        
        for (const auto& chunk : reader.chunks()) {
            // Check for duplicate chunk_index
            if (seen_indices.find(chunk.chunk_index) != seen_indices.end()) {
                return false;  // Duplicate found
            }
            seen_indices.insert(chunk.chunk_index);
            
            // Verify checksum
            if (chunk.checksum == 0) {
                return false;  // Invalid checksum
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace filegroup
