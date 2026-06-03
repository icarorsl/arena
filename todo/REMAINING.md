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

## ❌ Production hardening (not in Phase 1 spec)

| Item | Notes |
|---|---|
| TOML parser completion | Handle `[[array_of_tables]]` syntax properly |
| Real encryption key management | `KeyManager` class, load 32-byte key files |
| Multi-node Raft cluster | Currently single-node only |
| Proper logging | Structured JSON logging (currently `std::cout`/`std::cerr`) |
| Windows build support | POSIX-only (pread/pwrite/fdatasync) |
