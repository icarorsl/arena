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
| `docker-compose.yml` | ✅ |
| `DemoLocal` connects via http:// | ✅ |

### 🆕 Beyond Spec
| Component | Status |
|---|---|
| Step 16 — Expiry scanner | ✅ Background thread every 60s, marks expired versions VERSION_DELETED via Raft |
| Step 15 — Heartbeat | ✅ Pings storage nodes every 5s, HEALTHY→SUSPECT→DEAD FSM, writes NODE_HEALTH to Raft |
| Step 18 — Recovery | ✅ Raft log replay on startup, data survives restarts |
| Web Dashboard | ✅ Tables, Files, Upload, Delete, Play, Status, dark theme |
| Dynamic table creation | ✅ `CreateTable` RPC + Raft-replicated manifest entries |
| `created_at_us` timestamps | ✅ On files and versions, formatted in dashboard |
| Anti-forgery fix | ✅ `_ViewImports.cshtml` enables tag helpers, `_ViewStart.cshtml` enables layout |
| Video streaming | ✅ gRPC→HTTP chunked streaming, no buffering |
| Metrics wired | ✅ `MetricsServer` started in engine, wired to upload/read operations |
