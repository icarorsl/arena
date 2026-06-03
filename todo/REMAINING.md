# FILE Group — Remaining Work

## ⚠️ Stubbed (Steps 14–19)

| Step | Component | What's missing |
|---|---|---|
| 14 | Versioning enforcement | Dedicated integration tests for max_versions auto-deletion; per-version `VERSION_DELETED` manifest entries on file delete (currently only writes single `FILE_DELETED`) |
| | | **Current delete behavior:** Manifest-only. `FILE_DELETED` → in-memory `VersionState::DELETED`. Segment file headers and chunk data on disk are untouched. Physical disk reclamation needs Step 17 (compaction). |
| 16 | Expiry scanner + cleanup | Page segment scan, standard segment expiry scan, cleanup worker pool |
| 17 | Segment compaction | Read non-deleted chunks, rewrite to new segment, update locations |
| 19 | Background scrubbing | Integrity check per segment, CRC32C/GCM verification, corruption marking |

## 🆕 Dashboard & API gaps

| Item | Notes |
|---|---|
| Restore/undelete endpoint | Reverse a delete marker — restore versions to previous state (S3-style) |
| Per-version delete UI | `DeleteVersion` RPC works but dashboard has no UI for it |
| Serve correct MIME type | Sniff magic bytes to set Content-Type for Play page (mp4, webm, etc.) |

## ❌ Production hardening (not in Phase 1 spec)

| Item | Notes |
|---|---|
| TOML parser completion | Handle `[[array_of_tables]]` syntax properly |
| Real encryption key management | `KeyManager` class, load 32-byte key files |
| Multi-node Raft cluster | Currently single-node only |
| Proper logging | Structured JSON logging (currently `std::cout`/`std::cerr`) |
| Windows build support | POSIX-only (pread/pwrite/fdatasync) |
