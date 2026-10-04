# FILE Group — Remaining Work

## ⚠️ Stubbed (Step 19)

| Step | Component | What's missing |
|---|---|---|
| 19 | Background scrubbing | Integrity check per segment, CRC32C/GCM verification, corruption marking |

## 🆕 Dashboard & API gaps

| Item | Notes |
|---|---|
| Restore/undelete endpoint | Reverse a delete marker — restore versions to previous state (S3-style) |
| Serve correct MIME type | Sniff magic bytes to set Content-Type for Play page (mp4, webm, etc.) |
| UpdateTable gRPC + UI | Modify table config (name, expiry, max_versions, chunk_size). Lazy enforcement — expiry scanner picks up new `file_expires_in_days` on next 60s cycle; `max_versions` only affects future `complete_session` calls. |
| File.cshtml null ref | Line 7 crashes when FileInfo is null (stale data after compaction) |
| Health-aware chunk placement | Writes can fail when a storage node is down: `assign_chunks` uses all configured nodes, so with `replication_factor=3` on a 3-node cluster a down node may be chosen as primary. Needs unhealthy-node exclusion and/or reduced-replication fallback (ties into re-replication). |

## ❌ Production hardening (not in Phase 1 spec)

| Item | Notes |
|---|---|
| TOML parser completion | Handle `[[array_of_tables]]` syntax properly |
| Real encryption key management | `KeyManager` class, load 32-byte key files |
| Cluster hardening | Multi-node Raft + chunk replication now work over gRPC (see `test_cluster_engine`). Remaining: mTLS for cluster-internal RPCs, automatic re-replication on node failure |
| Proper logging | Structured JSON logging (currently `std::cout`/`std::cerr`) |
| Windows build support | POSIX-only (pread/pwrite/fdatasync) |
