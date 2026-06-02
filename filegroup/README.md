# FILE Group — Phase 1 Implementation

High-performance C++ distributed file storage system with Raft-replicated metadata, chunk-based replication, AES-256-GCM encryption, and page-based expiry.

## Architecture

- **Registry:** 3-node Raft cluster for metadata consensus
- **Storage Nodes:** Chunk storage with standard and page-based segments
- **Engine:** Client-facing service with upload/download, versioning, and encryption
- **Manifest:** Append-only log of operations, replicated via Raft
- **File Index:** In-memory state from manifest replay

## Build

```bash
mkdir build && cd build
cmake ..
make
make test
```

## Dependencies

- C++20 compiler (GCC 10+, Clang 12+, or MSVC 2019+)
- CMake 3.20+
- OpenSSL (libssl-dev)
- gRPC and Protobuf
- Google Test (optional, for tests)
- TOML++ (optional, will use fallback)

On Ubuntu/Debian:
```bash
sudo apt-get install libssl-dev libgrpc++-dev protobuf-compiler-grpc libprotobuf-dev libgtest-dev cmake
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
