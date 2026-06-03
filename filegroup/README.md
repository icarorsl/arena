# FILE Group — Phase 1 Implementation

High-performance C++ distributed file storage system with Raft-replicated metadata, chunk-based replication, AES-256-GCM encryption, and page-based expiry.

## Quick Start (Docker)

```bash
docker compose up -d                    # Start engine on :8443
cd ../clients/csharp && dotnet run --project DemoLocal   # Run demo
cd ../clients/csharp && dotnet run --project WebDashboard # Web UI at :5001
```

## Quick Start (Native)

```bash
mkdir build && cd build && cmake .. && make -j$(nproc)
./engine_grpc                           # Start engine on :8443
cd ../../clients/csharp && dotnet run --project DemoLocal
```

## Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
make test                               # 18 C++ tests
```

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

## Phase 1 Status

- [ ] Step 1: Common types and CRC32C ← **CURRENT**
- [ ] Step 2: Static config loader
- [ ] Step 3: TLS certificate generation
- [ ] Step 4: File header
- [ ] Step 5: Manifest format
- [ ] Step 6: File index
- [ ] Step 7: Registry Raft
- [ ] Step 8: Storage node gRPC
- [ ] ... (22 steps total)

## Known Limitations (Phase 1)

- No Phase 2+ features (edge nodes, LRU cache, streaming reads, re-replication)
- Raft log not compacted (manifest replay handles recovery)
- No corrupt chunk repair (detect and log only)
- No projection/rendition logic

## License

TBD
