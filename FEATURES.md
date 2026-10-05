# Arena — Feature Inventory

Single source of truth for what Arena actually does today. For step-by-step
history see [`todo/COMPLETED.md`](./todo/COMPLETED.md); for open work see
[`todo/REMAINING.md`](./todo/REMAINING.md).

**Legend**

| Mark | Meaning |
|---|---|
| ✅ | Implemented and working |
| ⚠️ | Partial — works today, with documented caveats |
| 🔜 | Planned / deferred — intentionally out of current scope (Phase 2+ or backlog) |
| ❌ | Not implemented — in-scope gap or known defect |

---

## Client API — Engine gRPC (`filegroup/proto/engine.proto`, port 8443)

| Feature | RPC(s) | Status |
|---|---|---|
| New upload | `OpenSession` | ✅ |
| Resume interrupted upload | `ResumeSession` | ✅ |
| Write chunk (idempotent retry) | `WriteChunk` | ✅ |
| Finalize upload (file becomes readable) | `CompleteSession` | ✅ |
| Cancel upload | `CancelSession` | ✅ |
| Stream whole file | `ReadFile` (server-streaming) | ✅ |
| Read a single chunk | `ReadChunk` | ✅ |
| Byte-range read (seeking / HTTP Range feeding) | `ReadRange` | ✅ |
| Delete file / single version | `DeleteFile`, `DeleteVersion` | ✅ |
| List files / versions | `ListFiles`, `ListVersions` | ✅ |
| File metadata | `GetFileInfo` | ✅ |
| Create table / list tables | `CreateTable`, `GetTables` | ✅ |
| Update table config | — | 🔜 planned |
| Segment introspection | `ListSegments` | ✅ |
| Cluster/node introspection | `GetNodeStatus` | ✅ |

## Storage & data model

| Feature | Status | Notes |
|---|---|---|
| Chunked storage in segment files | ✅ | 64-byte file header, `pwrite`/`pread` |
| Page-based segments (time-bucketed expiry) | ✅ | DAY / WEEK / MONTH buckets |
| Ring-based primary + replica placement | ✅ | `distribution/` |
| Multi-node chunk replication (rf ≥ 1) | ✅ | over gRPC, see Cluster |
| AES-256-GCM encryption at rest | ✅ | deterministic nonce, auth-tag verify |
| Encryption key management | 🔜 planned | `KeyManager` / key files |
| Versioning + `max_versions` enforcement | ✅ | oldest complete version auto-deleted |
| Two-phase delete + space reclaim | ✅ | MARKED_DELETED → compaction → DELETED |
| Manifest / file index (Raft state machine) | ✅ | replay on restart |
| Raft log compaction | 🔜 planned | log not compacted; manifest replay handles recovery |

## Cluster (multi-node)

| Feature | Status | Notes |
|---|---|---|
| Multi-voter Raft (election, log, commit quorum) | ✅ | `GrpcRaftTransport` |
| Metadata replication across nodes | ✅ | `RegistryGrpcService` |
| Write forwarding to the leader | ✅ | followers redirect writes over gRPC |
| Replicated reads on any node | ✅ | served from the local state machine |
| Chunk replication across nodes | ✅ | `StorageGrpcService` / `GrpcStorageClient` |
| 3-node Docker cluster | ✅ | `docker-compose.yml`, `make up` |
| Automatic re-replication after node loss | 🔜 planned | Phase 2 robustness |
| Health-aware chunk placement | 🔜 planned | today writes can fail while a storage node is down (placement ignores node health) |
| mTLS for cluster-internal RPCs | 🔜 planned | cluster RPCs are plaintext for now |

## Operations / background services

| Feature | Status | Notes |
|---|---|---|
| Heartbeat / node health FSM | ✅ | HEALTHY → SUSPECT → DEAD, written to Raft |
| Expiry scanner | ✅ | every 60s, marks expired versions |
| Segment compaction | ✅ | every 60s, live-chunk rewrite + atomic rename |
| Restart recovery | ✅ | Raft log replay rebuilds the index |
| Background scrubbing / integrity check | ❌ | Step 19 — stubbed (in-scope, not yet built) |
| Prometheus metrics | ✅ | HTTP `:9090` (per-node `9090/9091/9092`) |
| Structured logging | 🔜 planned | production hardening; currently `std::cout`/`std::cerr` |

## Tools

| Tool | Status | Notes |
|---|---|---|
| `dbctl` CLI | ⚠️ | `tls init`, `cluster status/nodes`, `files list/info/versions/delete` work, but run in-process; a network client is 🔜 planned |
| Docker engine image | ✅ | `filegroup/Dockerfile` |
| `make` targets | ✅ | `up`, `down`, `clean`, `rebuild`, `verify`, `dash` |

## C# SDK & Web Dashboard (`clients/csharp/`)

| Feature | Status | Notes |
|---|---|---|
| `FileGroupClient` wrapper (mTLS + API key) | ✅ | 12/12 contract tests |
| `Demo` (mTLS) / `DemoLocal` (plaintext) | ✅ | `DemoLocal` supports cross-node read verification |
| Dashboard: Tables / Files / Upload / File / Play / Segments | ✅ | Razor Pages at `:5001` |
| Dashboard: multi-node config + failover | ✅ | `Engine:Nodes`, retries next node on `Unavailable` |
| Dashboard: cluster Status (roles, commit index) | ✅ | |
| Dashboard: replication check (MATCH/FAIL per node) | ✅ | UI form of `make verify` |
| Dashboard: correct MIME sniffing for Play | 🔜 planned | backlog |
| Dashboard: restore / undelete | 🔜 planned | backlog |
| Dashboard: UpdateTable UI | 🔜 planned | depends on UpdateTable RPC |
| `File.cshtml` null-ref on stale data | ❌ | known defect after compaction |

## Configuration (`filegroup/config.toml`)

| Area | Keys | Status | Notes |
|---|---|---|---|
| Raft timing | `election_timeout_ms`, `heartbeat_interval_ms` | ✅ | |
| TLS files | `[tls]` ca/engine cert + key | 🔜 planned | generated by `dbctl`; server-side wiring pending |
| Registry nodes | `[registry_nodes]` id/address/certs | ✅ | via env in cluster mode |
| Storage nodes | `[storage_nodes]` id/address/role/region | ✅ | via env in cluster mode |
| File groups | `[groups]` chunk size, replication factor, max versions, expiry, encryption | ✅ | |
| Tables | `[tables]` per-table overrides | ✅ | dynamic tables via Raft |
| API keys | `[api_keys]` key/name/groups/permissions | 🔜 planned | parsed but not yet enforced |
| TOML array parsing | `[[array_of_tables]]` | 🔜 planned | incomplete; cluster mode uses env vars |

## Notes

- Items marked 🔜 are deferred by design (Phase 2+ or backlog tracked in
  [`todo/REMAINING.md`](./todo/REMAINING.md)), not oversights. Items marked ❌
  are in-scope work or defects still outstanding.
- The engine currently serves plaintext gRPC; server-side TLS and API-key
  enforcement are the main deferred security pieces.
- `config.toml` is not the runtime source of truth: `main.cc` builds config
  programmatically and reads cluster settings from environment variables.
- Windows build support is deferred (POSIX `pread`/`pwrite`/`fdatasync`).
