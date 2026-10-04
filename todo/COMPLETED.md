# FILE Group — Completed Steps

## ✅ Done (Steps 1–13, 20)

| Step | Component | Key deliverables |
|---|---|---|
| 1 | Types, CRC32C, clock | All enums, CRC32C hardware + software, `now_us()` |
| 2 | Config loader + resolver | `ClusterConfig`, TOML parser, three-level resolver |
| 3 | TLS cert generation | `dbctl tls init` via OpenSSL C API |
| 4 | File header | 64-byte binary header, serialize/deserialize/validate |
| 5 | Manifest format | All entry types, writer (`fdatasync`), reader (`replay`) |
| 6 | In-memory file index | `FileIndex` with `shared_mutex`, all apply/query methods |
| 7 | Raft consensus | Leader election (150-300ms), log replication, heartbeats |
| 8 | Segment files | 64-byte header, `pwrite`/`pread`, page bucket naming |
| 9 | Storage node service | `StorageServer`/`StorageClient`, StoreChunk/FetchChunk |
| 10 | Chunk distribution | Ring-based deterministic primary + replica assignment |
| 11 | AES-256-GCM encryption | Deterministic nonce, encrypt/decrypt, auth tag verification |
| 12 | Upload protocol | `Engine::open_session/write_chunk/complete_session` |
| 13 | Read protocol | `Engine::read_file/read_chunk`, replica failover |
| 20 | Engine gRPC server | `engine_grpc` binary, gRPC on :8443, all RPCs wired |
| 21 | Prometheus metrics | HTTP endpoint on :9090, gauges/counters/histograms |
| 22 | `dbctl` CLI | `tls init`, `cluster status`, `files list/info/versions/delete` |

### C# Client
| Component | Status |
|---|---|
| Proto compilation (`engine.proto` → C#) | ✅ |
| `FileGroupClient` wrapper (mTLS + API key) | ✅ |
| Proto contract tests (12/12) | ✅ |
| Demo app (mTLS) | ✅ |
| DemoLocal app (no TLS, for Docker/dev) | ✅ |
| End-to-end gRPC test (C# → C++) | ✅ |

### Docker
| Component | Status |
|---|---|
| `Dockerfile` for engine | ✅ |
| `docker-compose.yml` | ✅ 3-node cluster (node1 :8443, node2 :8444, node3 :8445) |
| `DemoLocal` connects via http:// | ✅ Optional second address reads from another node |

### 🆕 Multi-node cluster (Raft + chunk replication over gRPC)
| Component | Status |
|---|---|
| Multi-voter Raft | ✅ `propose()` waits for majority commit + apply; 3-voter election/replication unit test (`test_raft.cc`) |
| gRPC Raft transport | ✅ `GrpcRaftTransport` (RequestVote/AppendEntries over `registry.proto`) + `RegistryGrpcService` |
| Leader forwarding | ✅ Registry nodes forward writes to the leader over gRPC; followers serve replicated reads |
| gRPC storage transport | ✅ `StorageGrpcService` + `GrpcStorageClient` (chunk replication across nodes) |
| Cluster orchestration | ✅ `ClusterNode` wires per-node registry + storage + engine; env-driven (`FILEGROUP_NODE_ID`, `FILEGROUP_REGISTRY_NODES`, `FILEGROUP_STORAGE_NODES`, `FILEGROUP_REPLICATION`) |
| Docker cluster | ✅ `make up` builds/starts 3 nodes; `make verify` uploads to node1, reads back from node2 |
| Reliability fixes | ✅ Config-accurate storage node IDs; local-only compaction/rescan; heartbeat first-sweep delay; `EngineServer` teardown joins background threads before registry/engine destruction |
| Integration tests | ✅ `ClusterReplication` (Raft + storage over gRPC), `ClusterEngine` (3-node upload → cross-node read) |

### 🆕 Dashboard cluster-awareness
| Component | Status |
|---|---|
| `GetNodeStatus` RPC | ✅ Engine reports node id, leader/follower role, leader id, commit index and last applied index |
| Failover | ✅ Dashboard holds a channel per `Engine:Nodes` and retries the next node on `Unavailable` (unary + server-streaming; streaming retries only before the first response) |
| Cluster Status page | ✅ Lists every node with Raft role, leader id and commit/apply index, or UNREACHABLE |
| Replication check page | ✅ Uploads a probe file to the active node, reads it back from every node, reports MATCH/FAIL per node |
| Proto sync | ✅ C# `engine.proto` copies refreshed to match `filegroup/proto/engine.proto` |

### 🆕 Beyond Spec
| Component | Status |
|---|---|
| Step 14 — max_versions enforcement | ✅ Auto-deletes oldest versions in `CompleteSession` when count exceeds configured limit |
| Step 14 — per-version delete | ✅ `delete_file` writes individual `VERSION_DELETED` per version (no bulk `FILE_DELETED`) |
| Step 15 — Heartbeat | ✅ Pings storage nodes every 5s, HEALTHY→SUSPECT→DEAD FSM, writes NODE_HEALTH to Raft |
| Step 16 — Expiry scanner | ✅ Background thread every 60s, marks expired versions VERSION_DELETED via Raft |
| Step 18 — Recovery | ✅ Raft log replay on startup, data survives restarts |
| Data directory fix | ✅ Uses `/var/lib/filegroup` (Docker volume), respects `FILEGROUP_DATA_DIR` env var |
| Step 17 — Compaction | ✅ Background thread every 60s, scans segments, removes dead chunks, rewrites live chunks, atomic rename |
| Step 17 — Compaction fixes | ✅ Chunk offset uses header (not data) offset; MARKED_DELETED excluded from liveness → reclaimed; final .seg path after rename |
| Two-phase delete | ✅ MARKED_DELETED (yellow, playable) → compaction reclaims data → VERSION_RECLAIMED → DELETED (red, gone) |
| ChunkConfirmedEntry replicas | ✅ Persist segment_file + offset in Raft log for play-after-restart fallback |
| Play fix — offset | ✅ `rebuild_chunk_locations` stores header offset, not data offset |
| Play fix — gRPC size | ✅ MaxReceiveMessageSize 256MB for large file downloads |
| Web Dashboard | ✅ Tables, Files, Upload, Delete, Play, Status, dark theme |
| Dashboard polish | ✅ Yellow `.badge.warn` for MARKED_DELETED, back-link preserves table/group, Play page shows metadata overlay |
| Dynamic table creation | ✅ `CreateTable` RPC + Raft-replicated manifest entries |
| Per-version delete UI | ✅ ✕ button per version on file detail page with confirmation dialog |
| `created_at_us` timestamps | ✅ On files and versions, formatted in dashboard |
| Anti-forgery fix | ✅ `_ViewImports.cshtml` enables tag helpers, `_ViewStart.cshtml` enables layout |
| Video streaming | ✅ gRPC→HTTP chunked streaming, no buffering |
| Metrics wired | ✅ `MetricsServer` started in engine, wired to upload/read operations |
| Segment compaction (Step 17) | ✅ Background `CompactionService` every 60s, scans .seg files, filters dead chunks via FileIndex, rewrites compacted segments, updates Engine chunk locs |
