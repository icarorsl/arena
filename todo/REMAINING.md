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

## ❌ Not started (gRPC-inter-node)

| Component | What's needed |
|---|---|
| gRPC between Engine → Storage | `storage.proto` compiled, gRPC storage server wrapping `StorageServer` |
| gRPC between Registry nodes | `registry.proto` compiled, gRPC Raft transport replacing `InProcessRaftTransport` |
| mTLS everywhere | Replace `InsecureServerCredentials` with real TLS credentials |

## ❌ Not started (Production readiness)

| Item | Notes |
|---|---|
| TOML parser completion | Handle `[[array_of_tables]]` syntax properly |
| Real encryption key management | `KeyManager` class, load 32-byte key files |
| Multi-node Raft cluster | Currently single-node only |
| Proper logging | Structured JSON logging (currently `std::cout`/`std::cerr`) |
| Windows build support | POSIX-only (pread/pwrite/fdatasync) |
