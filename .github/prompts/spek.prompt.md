---
name: spek
description: Describe when to use this prompt
---

<!-- Tip: Use /create-prompt in chat to generate content with agent assistance -->

## Context

You are implementing **Phase 1** of a high-performance C++ database FILE group. This is a purpose-built large binary object store with chunk-based distributed storage, Raft-replicated metadata, versioning, encryption, and page-based expiry.

**Do not implement Phase 2, 3, or 4 features.** Any struct field, config property, or manifest entry marked `[Phase 2]` or later must be declared (for forward compatibility) but not implemented. Stub them with `// Phase 2 — not implemented` comments.

**Language and platform:** C++20, multi platform (linux, windows, etc), CMake build system.

**External dependencies allowed:**
- gRPC + Protobuf (transport and serialization)
- OpenSSL (AES-256-GCM encryption, SHA-256 nonce derivation, TLS certificates)
- TOML++ or cpptoml (static config file parsing)
- Prometheus cpp client (metrics)
- Google Test (unit tests)
- No other third-party dependencies — implement Raft, CRC32C, and all storage logic from scratch.

---

## Project Structure

Create the following directory layout:

```
filegroup/
├── CMakeLists.txt
├── README.md
├── proto/
│   ├── engine.proto          # Client → Engine service
│   ├── storage.proto         # Engine → Storage node service
│   └── registry.proto        # Engine → Registry service
├── src/
│   ├── main.cc               # Engine entry point
│   ├── config/
│   │   ├── config.h          # FileGroupConfig, FileTableConfig structs
│   │   ├── config.cc         # Config loading and validation
│   │   └── config_resolver.h # Three-level priority chain resolution
│   ├── common/
│   │   ├── types.h           # All enums: FileState, VersionState, EncryptionAlgo,
│   │   │                     #   ExpiryGranularity, NodeRole, ReplicaState, SegmentType
│   │   ├── crc32c.h/.cc      # CRC32C implementation (hardware-accelerated if available)
│   │   └── clock.h           # now_us() — Unix microseconds wall clock
│   ├── file_header/
│   │   ├── file_header.h     # FileHeader struct — 64-byte binary layout
│   │   └── file_header.cc    # Serialize/deserialize, version check
│   ├── manifest/
│   │   ├── manifest.h        # ManifestEntry, all entry type structs
│   │   ├── manifest_writer.h/.cc  # Append manifest entries, fdatasync
│   │   ├── manifest_reader.h/.cc  # Replay manifest log on startup
│   │   └── file_index.h/.cc  # LogicalFileEntry, VersionEntry, ChunkLocation,
│   │                         #   ReplicaLocation — in-memory index
│   ├── registry/
│   │   ├── raft.h/.cc        # Raft consensus — leader election, log replication
│   │   ├── registry_server.h/.cc  # gRPC registry service implementation
│   │   └── registry_client.h/.cc  # Engine-side registry client
│   ├── storage/
│   │   ├── segment.h/.cc     # Segment file — header, write cursor, pwrite
│   │   ├── page_segment.h/.cc # Page segment — expiry bucket, naming, header
│   │   ├── storage_server.h/.cc   # gRPC storage node service implementation
│   │   └── storage_client.h/.cc   # Engine-side storage node client
│   ├── engine/
│   │   ├── engine.h/.cc      # Engine — owns all subsystems, startup/shutdown
│   │   ├── session.h/.cc     # Upload session — state, chunk tracking
│   │   ├── upload.h/.cc      # OpenSession, WriteChunk, CompleteSession, ResumeSession
│   │   ├── read.h/.cc        # ReadFile, ReadChunk, replica selection, failover
│   │   ├── distribute.h/.cc  # Chunk distribution algorithm, node ring
│   │   └── engine_server.h/.cc    # gRPC engine service implementation
│   ├── encryption/
│   │   ├── encryption.h/.cc  # AES-256-GCM encrypt/decrypt, nonce derivation
│   │   └── key_manager.h/.cc # Key file loading, group/table key resolution
│   ├── expiry/
│   │   ├── expiry_resolver.h/.cc  # expires_in_days → expires_at resolution
│   │   ├── expiry_scanner.h/.cc   # Background page + standard segment scanner
│   │   └── cleanup_worker.h/.cc   # Chunk deletion worker pool
│   ├── compaction/
│   │   └── compaction.h/.cc  # Standard segment compaction on storage nodes
│   ├── health/
│   │   └── heartbeat.h/.cc   # Storage node heartbeat, HEALTHY/SUSPECT/DEAD
│   ├── recovery/
│   │   ├── engine_recovery.h/.cc  # Engine restart — manifest replay, session resume
│   │   └── node_recovery.h/.cc    # Storage node restart — segment scan, inventory
│   ├── scrub/
│   │   └── scrubber.h/.cc    # Background integrity check per storage node
│   ├── metrics/
│   │   └── metrics.h/.cc     # Prometheus metrics endpoint
│   ├── cli/
│   │   └── dbctl.cc          # Operational CLI tool
│   └── tls/
│       └── tls_init.cc       # Certificate generation CLI (dbctl tls init)
└── tests/
    ├── unit/
    │   ├── test_file_header.cc
    │   ├── test_crc32c.cc
    │   ├── test_config_resolver.cc
    │   ├── test_manifest.cc
    │   ├── test_encryption.cc
    │   ├── test_expiry_resolver.cc
    │   ├── test_distribute.cc
    │   ├── test_session.cc
    │   └── test_file_index.cc
    └── integration/
        ├── test_upload_complete.cc
        ├── test_upload_resume.cc
        ├── test_read_failover.cc
        ├── test_versioning.cc
        ├── test_expiry_page.cc
        ├── test_expiry_standard.cc
        ├── test_encryption_roundtrip.cc
        └── test_recovery.cc
```

---

## Implementation Order

**Follow this order exactly. Each step depends on the previous ones.**

---

### Step 1 — Common Types and CRC32C (Weeks 1–2, Part A)

Implement `src/common/types.h` defining all enums used throughout:

```cpp
enum class FileState       { ACTIVE, DELETED };
enum class VersionState    { UPLOADING, COMPLETE, SUPERSEDED, DELETED, EXPIRED,
                             SESSION_TIMED_OUT };
enum class EncryptionAlgo  { NONE, AES_256_GCM };
enum class ExpiryGranularity { DAY, WEEK, MONTH, UNSET };
enum class NodeRole        { ORIGIN, EDGE };      // EDGE = Phase 2, declared only
enum class ReplicaState    { PENDING, WRITTEN, VERIFIED, DELETE_PENDING, CORRUPT };
enum class SegmentType     { STANDARD, PAGE };
enum class NodeState       { HEALTHY, SUSPECT, DEAD };
```

Implement `src/common/crc32c.h/.cc`:
- Hardware-accelerated CRC32C using `__builtin_ia32_crc32*` intrinsics if available
- Software fallback using lookup table
- Single function: `uint32_t crc32c(const uint8_t* data, size_t len)`

Implement `src/common/clock.h`:
- `uint64_t now_us()` — current Unix time in microseconds
- `uint64_t now_us_monotonic()` — monotonic clock for timeouts

**Tests:** `test_crc32c.cc` — verify against known vectors. Test hardware and software paths.

---

### Step 2 — Static Config Loader (Weeks 1–2, Part B)

Implement `src/config/config.h` with exact structs from the spec (§1.3, §1.4, §7.3):

```cpp
struct FileGroupConfig { /* exact fields from spec §1.3 */ };
struct FileTableConfig { /* exact fields from spec §1.4 */ };
struct StorageNodeConfig {
    uint16_t    node_id;
    std::string address;
    NodeRole    role;         // Phase 2 — always ORIGIN in Phase 1
    std::string region;       // Phase 2 — informational in Phase 1
    std::string cert_file;
    std::string key_file;
};
struct RegistryNodeConfig { uint16_t id; std::string address; std::string cert_file; std::string key_file; };
struct TLSConfig           { std::string ca_cert_file; std::string engine_cert_file; std::string engine_key_file; };
struct ApiKeyConfig        { std::string key; std::string name; std::vector<uint32_t> groups; std::vector<std::string> permissions; };

struct ClusterConfig {
    TLSConfig                        tls;
    std::vector<RegistryNodeConfig>  registry_nodes;
    uint32_t                         election_timeout_ms;
    uint32_t                         heartbeat_interval_ms;
    std::vector<StorageNodeConfig>   storage_nodes;
    std::vector<FileGroupConfig>     groups;
    std::vector<FileTableConfig>     tables;
    std::vector<ApiKeyConfig>        api_keys;
};
```

Implement `src/config/config.cc`:
- `ClusterConfig load_config(const std::string& path)` — parse TOML, validate required fields, apply defaults
- Validation rules: group_ids unique, table group_ids exist, key files readable, replication_factor > 0, chunk_size >= min_chunk_bytes

Implement `src/config/config_resolver.h`:
- `uint64_t resolve_chunk_size(const FileGroupConfig&, const FileTableConfig*)` — three-level chain
- `uint8_t resolve_replication_factor(...)` — three-level chain
- `uint64_t resolve_expires_at(const FileGroupConfig&, const FileTableConfig*, uint32_t file_expires_in_days, uint64_t now_us)` — three-level chain, converts days to microseconds
- `ExpiryGranularity resolve_expiry_granularity(...)` — three-level chain
- `EncryptionAlgo resolve_encryption(...)` — table overrides group, bidirectional
- `std::string resolve_encryption_key_file(...)` — table key overrides group key
- `uint32_t resolve_max_versions(...)` — table overrides group

**Tests:** `test_config_resolver.cc` — test all combinations of group/table/file overrides including edge cases (0 = inherit vs 0 = no expiry — be precise about which zero means what for each property).

---

### Step 3 — TLS Certificate Generation (Weeks 1–2, Part C)

Implement `src/tls/tls_init.cc` — the `dbctl tls init` subcommand:

- Generate a cluster root CA (self-signed, 4096-bit RSA or P-256 ECDSA)
- Generate certificates for each node (engine, registry nodes, storage nodes) signed by the cluster CA
- Write all certs and keys to `--output` directory with correct filenames matching the config format
- Set permissions: cert files `chmod 644`, key files `chmod 400`
- Print a ready-to-paste TOML snippet for the `[tls]` section

Use OpenSSL C API directly — no shell invocations.

---

### Step 4 — File Header (Weeks 3–4, Part A)

Implement `src/file_header/file_header.h`:

```cpp
#pragma pack(push, 1)
struct FileHeader {
    uint8_t  file_header_version;  // must be 0x01
    uint64_t logical_file_id;
    uint64_t file_id;
    uint32_t version_number;
    uint16_t table_id;
    uint32_t group_id;
    uint64_t total_size;
    uint32_t chunk_count;
    uint64_t chunk_size;
    uint8_t  replication_factor;
    uint64_t expires_at;           // Unix microseconds, 0 = no expiry
    uint8_t  encryption;           // 0x00 = NONE, 0x01 = AES_256_GCM
    uint32_t content_checksum;     // CRC32C of full file plaintext
    uint8_t  reserved[3];          // must be zero
};
#pragma pack(pop)
static_assert(sizeof(FileHeader) == 64, "FileHeader must be exactly 64 bytes");
```

Implement `src/file_header/file_header.cc`:
- `void serialize(const FileHeader&, uint8_t* buf)` — write to 64-byte buffer, little-endian
- `FileHeader deserialize(const uint8_t* buf)` — read from buffer, validate version
- `bool validate(const FileHeader&)` — check version == 0x01, reserved bytes == 0, chunk_size > 0

**Tests:** `test_file_header.cc` — round-trip serialize/deserialize, unknown version detection, reserved byte validation.

---

### Step 5 — Manifest Format and Writer (Weeks 5–6)

Implement `src/manifest/manifest.h` with all entry type structs. Each entry has:

```cpp
struct ManifestEntryHeader {
    uint8_t  manifest_version;   // must be 0x01
    uint64_t entry_lsn;          // monotonically increasing per group
    uint64_t timestamp_us;
    uint32_t crc32c;             // CRC32C of bytes after this header
    uint16_t entry_type;         // ManifestEntryType enum
    uint16_t length;             // body length
};
```

Define structs for all Phase 1 entry types (§6.2):
- `SessionOpenEntry` — session_id, file_id, logical_file_id, table_id, group_id, version_number, resolved config fields, expected_chunks, expires_at, segment_type, page_bucket
- `ChunkConfirmedEntry` — session_id, file_id, chunk_index, chunk_size_actual, chunk_checksum, replicas (node_id + segment_file + offset)
- `VersionCompleteEntry` — file_id, logical_file_id, version_number, content_checksum, total_size, chunk_count
- `VersionDeletedEntry` — file_id, logical_file_id, version_number
- `FileDeletedEntry` — logical_file_id
- `SessionTimedOutEntry` — session_id, file_id
- `ChunkDeleteConfirmedEntry` — file_id, chunk_index, node_id
- `PageExpiredEntry` — group_id, table_id, expiry_bucket_us, granularity
- `PageDeletedEntry` — group_id, table_id, expiry_bucket_us, node_ids[]
- `NodeHealthEntry` — node_id, state
- `MaxVersionsEnforcedEntry` — logical_file_id, deleted_version_number, deleted_file_id
- Phase 3 entries (`PROJECTION_*`) — declare enum values, no struct bodies needed

Implement `src/manifest/manifest_writer.h/.cc`:
- `ManifestWriter` class — owns the manifest log file, append-only
- `uint64_t append(ManifestEntryType, const void* body, uint16_t length)` — writes header + body, calls `fdatasync()`, returns LSN
- LSN is a per-group monotonically increasing counter, persisted in the manifest file header
- Thread-safe — single writer (registry client thread calls this)

Implement `src/manifest/manifest_reader.h/.cc`:
- `void replay(const std::string& manifest_path, FileIndex& index)` — reads all entries sequentially, validates CRC32C per entry, applies each to the file index
- On CRC32C mismatch: log corruption, skip entry, continue replay (do not crash)
- On unknown entry type: skip entry body using `length` field (forward compatibility)

**Tests:** `test_manifest.cc` — write entries, replay, verify file index state. Test CRC32C corruption detection. Test unknown entry type skipping.

---

### Step 6 — In-Memory File Index (Weeks 5–6, parallel)

Implement `src/manifest/file_index.h/.cc` with exact structs from spec §6.3:

```cpp
struct ReplicaLocation {
    uint16_t     node_id;
    std::string  segment_file;
    uint64_t     offset;
    ReplicaState state;
    NodeRole     role;           // Phase 2 — always ORIGIN in Phase 1
    std::string  region;         // Phase 2 — empty in Phase 1
    uint16_t     projection_id;  // Phase 3 — always 0 in Phase 1
};

struct ChunkLocation {
    uint32_t                     chunk_index;
    uint64_t                     chunk_size_actual;
    uint32_t                     chunk_checksum;
    std::vector<ReplicaLocation> replicas;
};

struct VersionEntry {
    uint64_t                     file_id;
    uint32_t                     version_number;
    VersionState                 state;
    uint64_t                     expires_at;
    uint64_t                     total_size;
    uint32_t                     chunk_count;
    uint64_t                     chunk_size;
    uint8_t                      replication_factor;
    EncryptionAlgo               encryption;
    uint32_t                     content_checksum;
    uint64_t                     upload_session_id;
    SegmentType                  segment_type;
    std::string                  page_bucket;
    std::vector<ChunkLocation>   chunks;
    std::map<uint16_t,           ProjectionEntry> projections; // Phase 3 — empty
};

struct LogicalFileEntry {
    uint64_t                        logical_file_id;
    uint16_t                        table_id;
    uint32_t                        group_id;
    uint32_t                        latest_complete_version;
    uint32_t                        next_version_number;
    std::map<uint32_t, VersionEntry> versions;
};

class FileIndex {
public:
    // Apply manifest entries during replay and runtime
    void apply_session_open(const SessionOpenEntry&);
    void apply_chunk_confirmed(const ChunkConfirmedEntry&);
    void apply_version_complete(const VersionCompleteEntry&);
    void apply_version_deleted(const VersionDeletedEntry&);
    void apply_file_deleted(const FileDeletedEntry&);
    void apply_session_timed_out(const SessionTimedOutEntry&);
    void apply_chunk_delete_confirmed(const ChunkDeleteConfirmedEntry&);
    void apply_page_deleted(const PageDeletedEntry&);
    void apply_node_health(const NodeHealthEntry&);
    void apply_max_versions_enforced(const MaxVersionsEnforcedEntry&);

    // Queries
    const LogicalFileEntry* get_file(uint64_t logical_file_id) const;
    const VersionEntry* get_latest_complete(uint64_t logical_file_id) const;
    const VersionEntry* get_version(uint64_t logical_file_id, uint32_t version) const;
    std::vector<uint32_t> get_confirmed_chunks(uint64_t session_id) const;
    bool is_chunk_confirmed(uint64_t session_id, uint32_t chunk_index) const;
    std::vector<LogicalFileEntry> list_files(uint16_t table_id, uint32_t group_id) const;

    // ID generation
    uint64_t next_logical_file_id();
    uint64_t next_file_id();
    uint64_t next_session_id();

private:
    mutable std::shared_mutex                        mutex_;
    std::unordered_map<uint64_t, LogicalFileEntry>   files_;     // by logical_file_id
    std::unordered_map<uint64_t, uint64_t>           sessions_;  // session_id → file_id
    std::atomic<uint64_t>                            next_logical_file_id_{1};
    std::atomic<uint64_t>                            next_file_id_{1};
    std::atomic<uint64_t>                            next_session_id_{1};
    std::unordered_map<uint16_t, NodeState>          node_health_;
};
```

**Tests:** `test_file_index.cc` — apply a sequence of manifest entries, verify resulting index state. Test concurrent read/write with shared_mutex.

---

### Step 7 — Registry Raft Group (Weeks 7–10)

Implement `src/registry/raft.h/.cc` — a minimal Raft implementation sufficient for 3-node consensus:

**Required Raft behaviors:**
- Leader election with randomized timeouts (150ms–300ms)
- Log replication — leader appends entries, replicates to followers, commits when majority ack
- Leader heartbeats to prevent re-election (every 50ms)
- Term tracking — higher term always wins
- Vote granting — vote for candidate if term >= current and log is at least as up-to-date
- Follower log catch-up on reconnect

**Not required in Phase 1:**
- Log compaction / snapshotting (manifest replay handles recovery)
- Dynamic membership changes (static config)
- Pre-vote protocol

The Raft log entry payload is a serialized `ManifestEntry`. On commit, the Raft state machine calls `FileIndex::apply_*` with the deserialized entry.

Implement `src/registry/registry_server.h/.cc` — gRPC service:
- `AppendEntry` — accept manifest entry from engine, append to Raft log, wait for commit, return LSN
- `GetFile` — query file index, return FileMetadata
- `ListFiles` — paginated file listing from file index
- `GetSession` — return session state including confirmed chunk list
- `ReportNodeHealth` — update node health in file index
- `GetClusterHealth` — return all node health states

Implement `src/registry/registry_client.h/.cc` — engine-side client:
- Connection pool to all 3 registry nodes
- `AppendEntry` — send to current leader, retry on other nodes if leader fails
- Leader discovery — try all nodes, the leader responds; followers redirect
- All other query methods

---

### Step 8 — Segment Files (Weeks 11–12)

Implement `src/storage/segment.h/.cc` — standard segment files:

- Segment header exactly as specified in §5.1 — 64-byte, magic `0x444243_4348554E`, CRC32C of header
- `uint64_t write_chunk(file_id, chunk_index, chunk_data, chunk_checksum)` — acquires mutex, pwrite, advances write_offset, releases mutex, returns byte offset
- `ChunkData read_chunk(segment_file, offset, length)` — pread from offset
- `bool mark_deleted(offset)` — mark chunk entry as deleted for compaction tracking
- On open: read and validate segment header magic — reject silently-corrupt segments
- On create: write fresh segment header, fdatasync

Implement `src/storage/page_segment.h/.cc` — page segments:
- Page segment header exactly as specified in §4.4 — magic `0x444243_50414745`
- `std::string page_bucket_name(ExpiryGranularity, uint64_t expires_at_us)` — compute bucket string (e.g. "2026-06-03", "2026-W23", "2026-06")
- `std::string page_segment_filename(node_id, group_id, table_id, bucket)` — canonical name
- `uint64_t expiry_bucket_start_us(ExpiryGranularity, uint64_t expires_at_us)` — round expires_at down to bucket start
- Same write/read interface as standard segment
- `void unlink_page(const std::string& path)` — delete entire page file

**Tests:** Unit tests for `page_bucket_name` with all three granularities. Test that files expiring on different days within the same week land in the same WEEK bucket.

---

### Step 9 — Storage Node gRPC Service (Weeks 13–14)

Implement `src/storage/storage_server.h/.cc`:

- `StoreChunk` RPC — receives chunk bytes (ciphertext if encrypted), calls `segment.write_chunk()`, returns segment_file + offset
  - Route to correct segment: if file has an expiry, use page segment; else standard segment
  - Create new segment if active segment is full (write_offset >= segment_size_max)
  - Handle encrypted chunks: layout differs (nonce + ciphertext + auth_tag vs plaintext + CRC32C)
- `FetchChunk` RPC — pread from segment_file at offset, return raw bytes
- `DeleteChunk` RPC — mark chunk entry deleted in segment, return success
- `DeletePage` RPC — call `unlink_page(path)`, return success
- `Ping` RPC — return alive=true
- `ReportSegments` RPC — scan local segment headers, return inventory

All RPCs validate mTLS peer certificate before processing.

Implement `src/storage/storage_client.h/.cc`:
- Connection pool per storage node (persistent gRPC channels with mTLS)
- `StoreChunkResult store_chunk(node_id, ...)` — with timeout and error handling
- `ChunkData fetch_chunk(node_id, segment_file, offset, length)` — with timeout
- `bool delete_chunk(node_id, ...)` — best-effort, non-blocking
- `bool delete_page(node_id, path)` — synchronous, waits for confirm
- `NodeState ping(node_id)` — timeout-based health check

---

### Step 10 — Chunk Distribution Algorithm (Weeks 15–16)

Implement `src/engine/distribute.h/.cc`:

```cpp
struct ChunkAssignment {
    uint32_t chunk_index;
    uint16_t primary_node_id;
    std::vector<uint16_t> replica_node_ids;
};

// Compute assignments for all chunks of a file version
// Uses only HEALTHY ORIGIN nodes from health map
// Algorithm: primary = healthy_nodes[(file_id + chunk_index) % healthy_count]
//            replicas = next (replication_factor - 1) nodes in ring, skipping primary
std::vector<ChunkAssignment> assign_chunks(
    uint64_t file_id,
    uint32_t chunk_count,
    uint8_t  replication_factor,
    const std::vector<uint16_t>& healthy_origin_node_ids  // sorted by node_id
);
```

**Tests:** `test_distribute.cc` — verify no chunk has the same primary as another chunk (where node count allows). Verify replication_factor replicas per chunk. Test with node counts less than replication_factor. Test ring wrap-around.

---

### Step 11 — Encryption (Weeks 25–26)

Implement `src/encryption/encryption.h/.cc`:

```cpp
// Nonce derivation — deterministic per (file_id, chunk_index, group_id)
// nonce = sha256(file_id_le || chunk_index_le || group_id_le)[0:12]
std::array<uint8_t, 12> derive_nonce(uint64_t file_id, uint32_t chunk_index, uint32_t group_id);

// Encrypt plaintext chunk — returns ciphertext + appended 16-byte GCM auth tag
// Caller provides pre-derived nonce
std::vector<uint8_t> encrypt_chunk(
    const uint8_t* key_32bytes,
    const std::array<uint8_t, 12>& nonce,
    const uint8_t* plaintext,
    size_t plaintext_len
);

// Decrypt ciphertext chunk — verifies GCM auth tag
// Returns plaintext on success, throws on auth tag mismatch
std::vector<uint8_t> decrypt_chunk(
    const uint8_t* key_32bytes,
    const std::array<uint8_t, 12>& nonce,
    const uint8_t* ciphertext_with_tag,  // last 16 bytes are the GCM auth tag
    size_t total_len
);
```

Implement `src/encryption/key_manager.h/.cc`:
- `const uint8_t* get_key(uint32_t group_id, uint16_t table_id)` — resolve key: table key if set, else group key
- Load key files at startup, validate exactly 32 bytes
- Store keys in memory, never log or expose them

**Tests:** `test_encryption.cc` — encrypt/decrypt round-trip. Verify same inputs produce same ciphertext (deterministic nonce). Verify auth tag mismatch throws. Verify nonce derivation matches expected SHA-256 output.

---

### Step 12 — Upload Protocol (Weeks 19–24)

Implement `src/engine/session.h/.cc` — `UploadSession` class:

```cpp
struct UploadSession {
    uint64_t              session_id;
    uint64_t              file_id;
    uint64_t              logical_file_id;
    uint16_t              table_id;
    uint32_t              group_id;
    uint32_t              version_number;
    VersionState          state;
    uint64_t              created_at_us;
    uint64_t              last_activity_us;
    uint32_t              expected_chunks;   // 0 = unknown
    std::set<uint32_t>    confirmed_chunks;
    uint64_t              resolved_chunk_size;
    uint8_t               resolved_replication;
    uint64_t              resolved_expires_at;
    EncryptionAlgo        resolved_encryption;
    SegmentType           segment_type;
    std::string           page_bucket;
    std::vector<ChunkAssignment> chunk_assignments;
};
```

Implement `src/engine/upload.h/.cc`:

**`OpenSession`:**
1. Check admission control (max_concurrent_sessions, min_chunk_bytes)
2. Resolve config (chunk_size, replication_factor, expires_at, encryption) using config_resolver
3. Determine segment type (PAGE if expires_at > 0, STANDARD if permanent)
4. If PAGE: compute page_bucket from expiry and granularity
5. Generate session_id, file_id, logical_file_id (or use provided logical_file_id for new version)
6. Compute chunk_assignments using distribute algorithm
7. Write SESSION_OPEN to manifest via registry client
8. Store session in session map
9. Return resolved config to caller

**`WriteChunk`:**
1. Look up session — verify state == UPLOADING
2. Update last_activity_us
3. **Idempotency check** — if chunk_index in confirmed_chunks → return success immediately
4. Encrypt chunk if needed (derive nonce, call encrypt_chunk)
5. Send to primary node (StoreChunk RPC) — receive segment_file + offset
6. Primary node pipelines to replicas (storage node handles this internally)
7. Write CHUNK_CONFIRMED to manifest (includes replica locations)
8. Add chunk_index to confirmed_chunks in session
9. If expected_chunks > 0 and confirmed_chunks.size() == expected_chunks → auto-complete
10. Return chunk_ack

**`CompleteSession`:**
1. Verify all expected chunks confirmed (if expected_chunks > 0)
2. Verify content_checksum (CRC32C of all plaintext chunks in order)
3. Write VERSION_COMPLETE to manifest
4. Enforce max_versions — if COMPLETE version count > max_versions:
   - Find oldest COMPLETE version
   - Write MAX_VERSIONS_ENFORCED + VERSION_DELETED to manifest
   - Enqueue chunk cleanup for deleted version
5. Mark session closed
6. Return logical_file_id, file_id, version_number

**`ResumeSession`:**
1. Look up session_id in file index (or session map)
2. Return list of confirmed chunk indices
3. Client resumes from this point

**Pipeline replication** — the storage node's `StoreChunk` RPC must forward to replicas:
- `StoreChunk` on primary node includes `replica_nodes[]` in the request
- Primary node's gRPC handler calls `StoreChunk` on each replica before returning
- If any replica fails: return error to engine (engine returns error to client, client retries)

**Tests:** `test_upload_complete.cc`, `test_upload_resume.cc` — full upload round-trip, resume after simulated disconnect, idempotent retry of same chunk.

---

### Step 13 — Read Protocol (Weeks 29–30)

Implement `src/engine/read.h/.cc`:

**`ReadFile(logical_file_id, version?)`:**
1. Look up logical_file_id in file index
2. Resolve version — if not specified use latest_complete_version; else verify state is COMPLETE or SUPERSEDED
3. Return NOT_FOUND if no COMPLETE version exists
4. For each chunk_index in 0..chunk_count-1:
   a. Get replica list from file index
   b. Select replica (least-loaded HEALTHY node)
   c. Call FetchChunk on selected node
   d. Decrypt if needed — verify GCM auth tag or CRC32C
   e. Stream plaintext bytes to client
5. After all chunks: verify full content_checksum — return DATA_LOSS on mismatch

**Replica selection:**
- Maintain per-node atomic in-flight counter
- Increment before FetchChunk, decrement after
- Select HEALTHY node with lowest counter
- Fall back to SUSPECT if no HEALTHY available
- Error if all replicas DEAD

**Read failover:**
- On timeout or checksum error: retry on next available replica
- Transparent to client unless all replicas fail
- Log failover events, increment `file_read_failover_total` metric

**Tests:** `test_read_failover.cc` — simulate node failure during read, verify transparent retry.

---

### Step 14 — Versioning Enforcement (Weeks 27–28)

Implement max_versions enforcement in `CompleteSession` (see Step 12). Additionally implement version operations in the engine service:

- `ListVersions(logical_file_id)` — return all versions and states from file index
- `DeleteVersion(logical_file_id, version_number)` — write VERSION_DELETED to manifest, enqueue cleanup
- `DeleteFile(logical_file_id)` — write FILE_DELETED + VERSION_DELETED for all versions, enqueue cleanup

**Tests:** `test_versioning.cc` — upload 5 versions with max_versions=3, verify oldest 2 are auto-deleted. Verify explicit delete. Verify read of specific SUPERSEDED version.

---

### Step 15 — Node Health Heartbeat (Weeks 31–32)

Implement `src/health/heartbeat.h/.cc` — `HeartbeatThread` class:

- Single thread shared across all FILE groups
- Pings every storage node every `heartbeat_interval_sec` (5 seconds default) via `Ping` RPC
- State machine per node: HEALTHY → SUSPECT (3 missed) → DEAD (30 seconds no response) → HEALTHY (on recovery)
- On state change: write NODE_HEALTH manifest entry via registry client
- Exposes `NodeState get_node_state(node_id)` for use by upload and read path

---

### Step 16 — Expiry Scanner and Cleanup (Weeks 33–36)

Implement `src/expiry/expiry_scanner.h/.cc` — one thread per FILE group:

**Page segment scan (runs every `expiry_scan_interval_sec`):**
```
For each page segment tracked in file index where expiry_bucket_us <= now_us():
  1. Write PAGE_EXPIRED to manifest
  2. For each replica node: call DeletePage RPC (storage_client)
  3. Write PAGE_DELETED to manifest
  4. Call file_index.apply_page_deleted() — bulk update all versions in page to EXPIRED
```

**Standard segment scan:**
```
For each version in file index where:
  segment_type == STANDARD
  expires_at != 0 and now_us() >= expires_at
  state == COMPLETE or SUPERSEDED:

  1. Write VERSION_DELETED to manifest
  2. Enqueue to cleanup_worker
```

**Session timeout scan:**
```
For each session where:
  state == UPLOADING
  now_us() - last_activity_us > session_timeout_sec * 1_000_000:

  1. Write SESSION_TIMED_OUT to manifest
  2. Enqueue partial chunks to cleanup_worker
```

Implement `src/expiry/cleanup_worker.h/.cc` — 2 worker threads per FILE group:

```
For each version to clean (DELETED/EXPIRED/SESSION_TIMED_OUT, STANDARD segment):
  For each chunk:
    For each replica node:
      Call DeleteChunk RPC
      On success: write CHUNK_DELETE_CONFIRMED to manifest
      On failure: mark DELETE_PENDING, re-enqueue for retry
  When all chunks confirmed:
    Write VERSION_PURGED to manifest
    Remove version from file index
```

**Tests:** `test_expiry_page.cc`, `test_expiry_standard.cc` — write files with expiry, advance mock clock, run scanner, verify cleanup.

---

### Step 17 — Standard Segment Compaction (Weeks 37–38)

Implement `src/compaction/compaction.h/.cc` on the storage node side:

- Run compaction check after each chunk deletion
- If `deleted_bytes / total_segment_bytes >= compaction_threshold`:
  1. Read all non-deleted chunks from old segment
  2. Write to new segment (temp filename: `<original>.compact.tmp`)
  3. Sync new segment (`fdatasync`)
  4. For each moved chunk: call registry `AppendEntry(CHUNK_LOCATION_UPDATED)` — new segment_file + offset
  5. Rename new segment to canonical path
  6. Delete old segment
- Page segments are never compacted

---

### Step 18 — Engine Recovery (Weeks 39–40)

Implement `src/recovery/engine_recovery.h/.cc`:

```cpp
void recover_engine(Engine& engine, const ClusterConfig& config) {
    // 1. Connect to registry nodes via mTLS gRPC
    // 2. Find Raft leader (try each node, leader responds; followers redirect)
    // 3. Stream all manifest entries from leader
    // 4. Replay into file index
    // 5. Connect to all storage nodes, send Ping
    // 6. Update node health from ping results
    // 7. For each UPLOADING session: mark as resumable (clients may reconnect)
    // 8. For each SESSION_TIMED_OUT session: enqueue cleanup
    // 9. Start subsystems: heartbeat, expiry_scanner, cleanup_worker, scrubber
}
```

Implement `src/recovery/node_recovery.h/.cc`:

```cpp
void recover_storage_node(StorageNode& node, RegistryClient& registry) {
    // 1. Scan local segment file headers (read header only — no chunk data)
    // 2. Build local inventory: {file_id, chunk_index} → {segment_file, offset}
    // 3. Call registry ReportSegments with inventory
    // 4. Registry compares against manifest, identifies missing chunks
    // 5. Missing chunks → mark DELETE_PENDING on this node in manifest
    // 6. Node begins serving immediately
}
```

---

### Step 19 — Background Scrubbing (Weeks 41–42)

Implement `src/scrub/scrubber.h/.cc` — one thread per storage node:

- Configurable interval (default: 7 days = 604800 seconds)
- For each segment file: for each chunk entry in segment:
  - Read chunk bytes
  - If encrypted: decrypt and verify GCM auth tag
  - If unencrypted: compute CRC32C, compare to stored chunk_checksum in segment entry
  - On mismatch: log CORRUPTION event, write manifest entry marking replica CORRUPT, increment metric
- Do not repair — log and mark only (Phase 2)

---

### Step 20 — Engine gRPC Service (throughout, finalized Weeks 43–44)

Implement `src/engine/engine_server.h/.cc` — the client-facing gRPC service:

All RPCs validate API key via interceptor before reaching handler:
- Check `x-api-key` metadata header
- Look up in config's api_keys list
- Verify group access and required permission
- Reject with UNAUTHENTICATED or PERMISSION_DENIED

Implement all methods from the engine service:
- `OpenSession`, `ResumeSession`, `WriteChunk`, `CompleteSession`
- `ReadFile` (server-streaming), `ReadChunk`
- `DeleteFile`, `DeleteVersion`, `ListFiles`, `ListVersions`, `GetFileInfo`

---

### Step 21 — Metrics (Weeks 43–44)

Implement `src/metrics/metrics.h/.cc`:

- HTTP endpoint on configurable port (default: 9090) at `/metrics`
- Expose all Phase 1 metrics from spec §15.3 using Prometheus cpp client
- Key metrics to instrument:
  - `file_upload_sessions_active` — gauge, updated on OpenSession/CompleteSession
  - `file_upload_chunks_confirmed_total` — counter, incremented in WriteChunk
  - `file_chunk_write_latency_ms` — histogram, measured from WriteChunk start to CHUNK_CONFIRMED
  - `file_chunk_read_latency_ms` — histogram, measured from FetchChunk start to bytes returned
  - `file_node_health_state` — gauge per node_id, updated by heartbeat thread
  - `file_disk_used_bytes` — gauge per node, polled from storage nodes periodically
  - All other metrics from §15.3

---

### Step 22 — Operational CLI (Weeks 43–44)

Implement `src/cli/dbctl.cc` — subcommand CLI tool:

```
dbctl tls init --nodes N --output /path/
dbctl cluster status
dbctl cluster nodes
dbctl files list --group G --table T [--page-token T] [--page-size N]
dbctl files info --group G --file F
dbctl files versions --group G --file F
dbctl files delete --group G --file F [--version V]
dbctl sessions list --group G
dbctl sessions cancel --group G --session S
dbctl pages list --group G --table T
dbctl pages status --group G --table T --bucket BUCKET
dbctl nodes list
dbctl nodes health
dbctl registry status
dbctl registry leader
dbctl registry backup --output /path/snapshot.bin
```

Each command connects to the engine or registry via gRPC (mTLS) and formats output as a table or JSON (flag: `--output json`).

---

## Cross-Cutting Requirements

### Error Handling
- Never crash on malformed input — return appropriate gRPC status codes (§15.2)
- CRC32C corruption in manifest: log + skip + continue replay (do not abort)
- Unknown manifest entry type: skip using `length` field (forward compatibility)
- Unknown `file_header_version`: return fatal error to caller
- Network errors: retry with exponential backoff up to 3 attempts, then return UNAVAILABLE

### Thread Safety
- `FileIndex` uses `std::shared_mutex` — shared lock for reads, exclusive for writes
- Session map uses `std::mutex` — exclusive lock for all operations
- Segment write cursor uses `std::mutex` per segment — exclusive for writes, none for reads
- Registry client serializes manifest writes through a single-threaded queue
- Heartbeat thread updates node health atomically

### Forward Compatibility Rules
- All Phase 2+ struct fields must be declared with default values — never omit them
- `NodeRole role` in `ReplicaLocation` — always `ORIGIN` in Phase 1
- `std::string region` in `ReplicaLocation` — always empty in Phase 1
- `uint16_t projection_id` in `ReplicaLocation` — always `0` in Phase 1
- `std::map<uint16_t, ProjectionEntry> projections` in `VersionEntry` — always empty in Phase 1
- `file_header_version` reserved value `0x02` — reject with fatal error if encountered
- Manifest entry types `0x0C`–`0x0F` (Phase 3) — declare enum values, skip on replay

### Logging
- Use structured logging (JSON format) with fields: timestamp, level, component, message, key-value pairs
- Log levels: DEBUG, INFO, WARN, ERROR, FATAL
- FATAL: log then `std::abort()`
- Never log encryption keys, API keys, or chunk plaintext

### Testing Requirements
- Every public function must have at least one unit test
- Integration tests must use a real 3-node Raft registry (in-process, separate threads)
- Integration tests must use real segment files (temp directory, cleaned up after test)
- No mocking of the file system or gRPC transport in integration tests
- Target: all unit tests run in < 1 second; all integration tests run in < 30 seconds

---

## Key Invariants to Enforce

These invariants must hold at all times and must be verified in tests:

1. A chunk is never served to a client unless its `CHUNK_CONFIRMED` manifest entry has been Raft-committed (majority of registry nodes have acknowledged it).

2. A file version is never readable until its `VERSION_COMPLETE` manifest entry is Raft-committed.

3. `WriteChunk` with the same `(session_id, chunk_index)` is always idempotent — calling it N times is identical to calling it once.

4. The full file CRC32C (plaintext, all chunks in order) must be verified at `CompleteSession` and again after all chunks are read in `ReadFile`.

5. A page segment file is never rewritten or partially modified — it is append-only until wholesale deletion via `unlink()`.

6. A standard segment file is only `fallocate(PUNCH_HOLE)` or deleted after all readers have released their references (reference-counted).

7. The `max_versions` policy is enforced synchronously within `CompleteSession` — never deferred.

8. The `file_header_version` field is always the first byte read from any file header — no other field is interpreted if the version is unknown.

---

## Do Not Implement (Phase 2+)

- Edge node routing or behavior (`NodeRole::EDGE`)
- LRU chunk cache (`chunk_cache_mb`)
- Byte-range reads (`ReadRange`)
- Streaming reads (reading before upload completes)
- Re-replication after node failure (Phase 2)
- Dynamic node discovery (Phase 2)
- Key rotation (Phase 2)
- Corrupt chunk repair — detect and log only (Phase 2)
- Any projection, rendition, or materialization logic (Phase 3+)
- Built-in aggregation engine (Phase 4)
- Adaptive read routing (Phase 3+)
- Multi-engine coordination (Phase 2)