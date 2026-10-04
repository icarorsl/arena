---
name: spek2
description: use this prompt to generate a detailed implementation plan for the BATCH, NONE and ACID groups database project, including directory structure, implementation order, and specific tasks for each phase. Also use this prompt, when need to validate the existing implementation against this plan and against the remaining work in todo/REMAINING.md, and update the plan accordingly.
---

<!-- Tip: Use /create-prompt in chat to generate content with agent assistance -->

# Copilot Prompt: High-Performance C++ Database Implementation

You are an expert C++20 developer tasked with implementing a high-performance embedded database engine. This prompt contains the complete specification. Read it carefully and follow the implementation order strictly.

## Context

You're building a database engine targeting **1M durable writes/sec** (BATCH mode) and **200k/sec** (ACID mode) and NONE mode for in memory on Linux (Ubuntu 22.04+). The codebase must be production-quality, with proper error handling, RAII, and modern C++ practices.

## Key Requirements

1. **Follow the Implementation Roadmap** (Section "Implementation Roadmap" at the end) - do NOT skip ahead
2. **Check all version fields** (`header_version`, `wal_version`, `checkpoint_version`) before parsing
3. **Never silently interpret unknown versions** - treat as fatal errors
4. **Use the exact data structures** specified - no deviation without justification
5. **Implement safety protocols** (especially the `mmap` + `PUNCH_HOLE` protocol in §6.2)

## Project Structure

Organize the code as follows:

```
include/
  database/
    types.h          # Basic types, enums, constants
    record.h         # Record header, payload format
    config.h         # GroupConfig, BatchPolicy
    hash_table.h     # Sharded lock-free hash table
    wal.h            # WAL format, writing, recovery
    data_file.h      # Segment files, mmap management
    checkpoint.h     # Checkpoint format and recovery
    group.h          # DurabilityGroup, GroupManager
    ttl.h            # TTL resolution, expiration scanner
    compaction.h     # Compaction algorithm
    query.h          # Cross-group queries, joins
    index.h          # Secondary B-tree indexes
    replication.h    # Replication support (Phase 2)
src/
  record.cpp
  hash_table.cpp
  wal.cpp
  data_file.cpp
  checkpoint.cpp
  group.cpp
  ttl.cpp
  compaction.cpp
  query.cpp
  index.cpp
  replication.cpp
tests/
  unit/
  integration/
  benchmark/
```

## Implementation Phases

### Phase 1: Foundations (Weeks 1-4)

Begin with these components in order:

**Week 1-2: Core Data Structures**
- Implement `RecordHeader` (45 bytes) with proper packing
- Implement page layout (8KB pages)
- Implement memory pool with bump allocator
- **Critical**: All reads must check `header_version` before interpreting fields

**Week 3-4: Hash Table**
- Implement sharded lock-free hash table (per group)
- Implement RCU read protocol with epoch counters
- Each shard has independent resize locking
- Must support 1M+ records per group

**Example Record Header Layout:**
```cpp
#pragma pack(push, 1)
struct RecordHeader {
    uint8_t  header_version;    // 0x01 for Phase 1
    uint64_t primary_key;
    uint32_t t_xmin;
    uint32_t t_xmax;            // 0 = live
    uint32_t t_cid;
    uint32_t infomask;          // HEAP_EXPIRED (0x0001), HEAP_DELETED (0x0002)
    uint64_t created_at_us;
    uint64_t expires_at_us;     // 0 = no expiry
    uint32_t value_length;      // Max 65535 bytes
};
#pragma pack(pop)
static_assert(sizeof(RecordHeader) == 45, "Header must be exactly 45 bytes");
```

### Phase 2: Persistence (Weeks 5-8)

**Weeks 5-6: WAL and Commit Protocol**
- Implement WAL format with all fields (including replication fields)
- Implement group commit protocol (both BATCH and ACID)
- **Always check `wal_version` before parsing WAL entries**

**Weeks 7-8: Data Files and Checkpoints**
- Implement segment files (256MB) with mmap
- Implement checkpoint (60s or 512MB WAL)
- **Checkpoint must verify `checkpoint_version` on recovery**

### Phase 3: Durability Groups (Weeks 9-12)

- Implement GroupManager singleton
- Enforce FD budget at startup (`getrlimit(RLIMIT_NOFILE)`)
- Implement online group reconfiguration state machine
- Implement group deletion (RESTRICT/CASCADE/ARCHIVE)

### Phase 4: TTL and Expiration (Weeks 13-16)

- Implement TTL resolution priority (record → table → group → none)
- Implement lazy expiration (reads check `expires_at_us`)
- Implement background expiration scanner (shared across groups)
- Implement threshold-based compaction scheduling (20% expired ratio)

### Phase 5: Concurrency (Weeks 13-16, parallel)

- Implement thread inventory (accept, worker, commit, compaction, scanner)
- Implement compaction thread pool with work-stealing
- Implement index backfill jobs (shared with compaction pool)

### Phase 6: Compaction (Weeks 15-16)

**CRITICAL: Safe mmap + PUNCH_HOLE Protocol**
```cpp
// THIS SEQUENCE IS MANDATORY - NO EXCEPTIONS:
void safe_replace_segment(Group* group, SegmentId old_id, SegmentId new_id) {
    // 1. Mark retiring
    group->segment_registry.begin_replace(old_id);
    
    // 2. Wait for readers to finish (reference count drops to 0)
    group->segment_registry.wait_readers_done(old_id);
    
    // 3. Unmap the segment
    munmap(old_segment->addr, old_segment->size);
    
    // 4. Only now can we punch holes or delete
    fallocate(old_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, ...);
}
```

### Phase 7: Cross-Group Queries (Weeks 17-20)

- Implement per-group read watermarks (LSN of most recent commit)
- Implement hash join execution (smaller side is build side)
- Implement materialized views (refreshed on configurable interval)

### Phase 8: Performance Tuning (Weeks 21-28)

- Implement SIMD optimizations for hash table operations
- Implement NUMA-aware memory allocation
- Implement io_uring for WAL writes
- Benchmark against targets: 1M BATCH writes/sec, 200k ACID writes/sec

## Critical Implementation Notes

### 1. Version Checking
**Always** check version fields before accessing structures:
```cpp
// Example - safe WAL reader
if (wal_entry.wal_version != 0x01) {
    throw FatalError("Unknown WAL version: " + std::to_string(wal_entry.wal_version));
}
// Now safe to parse rest of entry
```

### 2. FD Management
```cpp
// Startup check - mandatory
struct rlimit rl;
if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
    throw SystemError("getrlimit failed");
}
size_t required_fds = MAX_GROUPS * 4 + 1024; // 4 FDs per group + safety margin
if (rl.rlim_cur < required_fds) {
    throw ConfigError("FD limit too low. Required: " + std::to_string(required_fds));
}
```

### 3. Memory Pool Capacity
- Arena size: 256MB
- At 1M records: ~201MB needed (calculate from spec)
- Groups start with 1 arena, add more as needed
- VA reservation: 1024 groups × 256MB = 256GB (virtual only)

### 4. Concurrency Primitives
- Use `std::atomic` with appropriate memory orders
- Use epoch-based RCU for hash table reads
- No global locks in hot paths

### 5. Error Handling
- Unknown format versions: FatalError (do not recover)
- Out of memory: std::bad_alloc (let it propagate)
- FD exhaustion: ConfigError (recoverable with proper config)
- Corruption: Log and abort recovery

## Testing Requirements

1. **Unit Tests**: Every component must have unit tests
2. **Integration Tests**: WAL replay, checkpoint recovery, group reconfiguration
3. **Benchmark**: Measure against 1M/sec BATCH and 200k/sec ACID targets
4. **Fuzz Testing**: Random operations with crash recovery validation

## Example API Surface

```cpp
namespace database {

class Database {
public:
    // Group management
    GroupHandle create_group(const GroupConfig& config);
    void delete_group(GroupHandle handle, DeletionFlag flag = DeletionFlag::RESTRICT);
    
    // Table operations
    TableHandle create_table(GroupHandle group, const TableConfig& config);
    void drop_table(TableHandle table);
    
    // CRUD
    WriteResult insert(TableHandle table, uint64_t key, const Payload& value);
    WriteResult update(TableHandle table, uint64_t key, const Payload& value);
    WriteResult delete_(TableHandle table, uint64_t key);
    ReadResult get(TableHandle table, uint64_t key);
    
    // Batch operations
    BatchResult batch_write(TableHandle table, std::span<WriteOperation> ops);
    
    // Query
    QueryResult query(const std::string& sql, TableHandle... tables);
    
    // Materialized views
    ViewHandle create_view(const std::string& name, const std::string& query);
    void refresh_view(ViewHandle view);
};

} // namespace database
```

## Key Constants

```cpp
constexpr uint8_t  HEADER_VERSION_PHASE1 = 0x01;
constexpr uint8_t  WAL_VERSION_PHASE1 = 0x01;
constexpr uint8_t  CHECKPOINT_VERSION_PHASE1 = 0x01;
constexpr uint32_t PAGE_SIZE = 8192; // 8KB
constexpr uint32_t WAL_SEGMENT_SIZE = 64 * 1024 * 1024; // 64MB
constexpr uint32_t DATA_SEGMENT_SIZE = 256 * 1024 * 1024; // 256MB
constexpr uint32_t ARENA_SIZE = 256 * 1024 * 1024; // 256MB
constexpr uint16_t MAX_GROUPS = 1024; // configurable at compile time
constexpr uint32_t MAX_PAYLOAD_SIZE = 65535; // 64KB
constexpr uint32_t MAX_COLUMNS_PER_TABLE = 65535;
constexpr uint8_t  MAX_INDEXES_PER_TABLE = 255;
```

## Development Workflow

1. **Start with Phase 1** - implement foundations first
2. **Don't skip ahead** - each phase depends on previous ones
3. **Test incrementally** - write tests as you implement
4. **Profile early** - measure performance against targets
5. **Document APIs** - write Doxygen comments for all public APIs

## Success Criteria

- [ ] All unit tests pass
- [ ] Integration tests pass (recovery, reconfiguration)
- [ ] **BATCH writes/sec ≥ 1M** (measured with NVMe storage)
- [ ] **ACID writes/sec ≥ 200k** (measured with NVMe storage)
- [ ] No data corruption after crash recovery
- [ ] FD limit check at startup
- [ ] All version fields checked before parsing
- [ ] Safe mmap + PUNCH_HOLE protocol implemented

## Questions During Implementation

If you encounter ambiguities:
1. Re-read the relevant section carefully
2. Check the resolved design questions at the end
3. If still unclear, state your interpretation and proceed

**Remember**: This spec supersedes all previous versions (v1.0, v2.0). Do not implement from older specs.

---

*Begin with Phase 1 (Foundations) - Weeks 1-4. Start with RecordHeader and page layout. Do not skip ahead to WAL or groups until foundations are complete and tested.*