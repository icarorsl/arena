# FILE Group — Remaining Work

## ⚠️ Stubbed (Steps 14–19)

| Step | Component | What's missing |
|---|---|---|
| 14 | Versioning enforcement | Dedicated integration tests for max_versions auto-deletion |
| 15 | Heartbeat | Real thread that pings storage nodes, state machine HEALTHY→SUSPECT→DEAD |
| 16 | Expiry scanner + cleanup | Page segment scan, standard segment expiry scan, cleanup worker pool |
| 17 | Segment compaction | Read non-deleted chunks, rewrite to new segment, update locations |
| 18 | Engine + node recovery | Manifest replay on startup, segment inventory scan, missing chunk detection |
| 19 | Background scrubbing | Integrity check per segment, CRC32C/GCM verification, corruption marking |

## ❌ Production hardening (not in Phase 1 spec)

| Item | Notes |
|---|---|
| TOML parser completion | Handle `[[array_of_tables]]` syntax properly |
| Real encryption key management | `KeyManager` class, load 32-byte key files |
| Multi-node Raft cluster | Currently single-node only |
| Proper logging | Structured JSON logging (currently `std::cout`/`std::cerr`) |
| Windows build support | POSIX-only (pread/pwrite/fdatasync) |
