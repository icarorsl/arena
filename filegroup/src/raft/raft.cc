#include "raft/raft.h"

#include <cstring>
#include <stdexcept>
#include <fstream>

namespace filegroup {

static_assert(sizeof(RaftLogEntry) == 24, "RaftLogEntry must be 24 bytes");

RaftRegistry::RaftRegistry(const std::string& log_path, uint16_t local_node_id, uint32_t group_id)
    : log_path_(log_path), local_node_id_(local_node_id), group_id_(group_id) {
    // Initialize Raft state
    state_.current_term = 1;
    state_.voted_for = 0;
    state_.commit_index = 0;
    state_.last_applied_index = 0;

    // TODO: Read persisted Raft log from file
    // For now, create empty log
}

RaftRegistry::~RaftRegistry() = default;

uint64_t RaftRegistry::append_entry(const void* entry_data, uint16_t entry_length) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);

    RaftLogEntry entry;
    entry.term = state_.current_term;
    entry.index = state_.log.size() + 1;  // Next index
    entry.type = RaftEntryType::NOP;
    entry.data_length = entry_length;

    state_.log.push_back(entry);

    // TODO: Persist to disk
    // For now, just keep in memory

    return entry.index;
}

void RaftRegistry::commit_up_to(uint64_t index) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);

    if (index > state_.log.size()) {
        throw std::runtime_error("Cannot commit beyond log size");
    }

    state_.commit_index = index;

    // TODO: Apply entries from last_applied_index+1 to commit_index to file index
    // For now, just update the index
    state_.last_applied_index = index;
}

uint64_t RaftRegistry::next_log_index() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    return state_.log.size() + 1;
}

uint64_t RaftRegistry::last_log_term() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    if (state_.log.empty()) {
        return 0;
    }
    return state_.log.back().term;
}

}  // namespace filegroup
