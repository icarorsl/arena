# FILE Group — Phase 1 Implementation

High-performance C++ distributed file storage system with Raft-replicated metadata, chunk-based replication, AES-256-GCM encryption, and page-based expiry.

## Quick Start (Docker — 3-node cluster)

```bash
make up                                 # Build + start node1 :8443, node2 :8444, node3 :8445
make verify                             # Upload to node1, read back from node2 (proves replication)
cd ../clients/csharp && dotnet run --project WebDashboard # Web UI at :5001
```

Each node runs its own registry (Raft) node and storage node. Raft metadata
replication and chunk replication travel over gRPC on the internal Docker
network. `DemoLocal` takes an optional second address to read from a different
node:

```bash
cd ../clients/csharp
dotnet run --project DemoLocal -- http://localhost:8443 http://localhost:8444
```

## Quick Start (Native)

```bash
mkdir build && cd build && cmake .. && make -j$(nproc)
./engine_grpc                           # Start engine on :8443
cd ../../clients/csharp && dotnet run --project DemoLocal
```

## Build & Test (C++)

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
ctest --output-on-failure               # 20 tests (unit + integration)
ctest -R ClusterReplication --output-on-failure   # Raft + storage over gRPC
ctest -R ClusterEngine --output-on-failure        # 3-node upload → cross-node read
```

The `ClusterReplication`/`ClusterEngine` tests start real gRPC servers on
loopback ports and assert that a manifest entry and a chunk replicate to every
node. No Docker required.

## C# Client

```bash
cd clients/csharp
dotnet build FileGroup.slnx              # 4 projects
dotnet test FileGroup.slnx               # 12 C# tests

# Run demos:
dotnet run --project DemoLocal           # Docker/local (no TLS)
dotnet run --project Demo                # Production (mTLS + certs)
```

## CLI

```bash
./dbctl tls init --nodes 3 --output certs/
./dbctl cluster status
./dbctl files list --group 1 --table 1
./dbctl files info --group 1 --file 42
```

## Dependencies

- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.20+
- OpenSSL (libssl-dev)
- gRPC + Protobuf
- Google Test (optional, for tests)

On Ubuntu/Debian:
```bash
sudo apt-get install libssl-dev libgrpc++-dev protobuf-compiler-grpc libprotobuf-dev libgtest-dev cmake
```

On macOS:
```bash
brew install cmake openssl grpc protobuf googletest
```

## Project Structure

```
filegroup/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── proto/                  # gRPC service definitions
├── src/
│   ├── common/             # Types, CRC32C, clock
│   ├── cluster/            # Multi-node cluster orchestration (ClusterNode)
│   ├── config/             # Configuration loader and resolver
│   ├── file_header/        # 64-byte binary file header
│   ├── manifest/           # Manifest format, writer, reader, file index
│   ├── registry/           # Raft consensus, registry gRPC service
│   ├── storage/            # Segment files (standard + page)
│   ├── engine/             # Engine, upload, read, distribution
│   ├── encryption/         # AES-256-GCM, key manager
│   ├── expiry/             # Expiry scanner, cleanup worker
│   ├── compaction/         # Segment compaction
│   ├── health/             # Heartbeat monitoring
│   ├── recovery/           # Engine and storage node recovery
│   ├── scrub/              # Background integrity checking
│   ├── metrics/            # Prometheus metrics
│   ├── cli/                # dbctl command-line tool
│   ├── tls/                # Certificate generation
│   └── main.cc             # Engine entry point
└── tests/
    ├── unit/               # Unit tests
    └── integration/        # Integration tests
```

## Status

Steps 1–18, 20–22 are complete (see `../todo/COMPLETED.md`), including
multi-node Raft and chunk replication over gRPC. Step 19 (background
scrubbing) and production hardening (mTLS for cluster RPCs, re-replication,
health-aware chunk placement, structured logging) remain — see
`../todo/REMAINING.md`.

## Known Limitations (Phase 1)

- Multi-node Raft and chunk replication work (over gRPC), but **automatic
  re-replication** after a node failure is not implemented yet
- No Phase 2+ features (edge nodes, LRU cache)
- Cluster-internal RPCs are insecure (mTLS wiring is still pending)
- Raft log not compacted: no snapshots, log never truncated (manifest replay handles recovery)
- No corrupt chunk repair (detect and log only)
- No projection/rendition logic

## License

TBD
