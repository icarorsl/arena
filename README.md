# Arena — Distributed File Storage

Arena is a high-performance distributed file storage system built in C++ with a C# gRPC client SDK.

| Component | Dir | Language | Status |
|---|---|---|---|
| Engine | `filegroup/` | C++20 | ✅ Phase 1 (Steps 1–13, 20–22) |
| C# Client | `clients/csharp/` | C# / .NET 8 | ✅ 12/12 tests passing |
| Web Dashboard | `clients/csharp/WebDashboard/` | ASP.NET Core Razor Pages | ✅ Browsing live |
| Docker | `filegroup/docker-compose.yml` | Docker | ✅ 3-node cluster, replication working |

## Quick Start (3-node cluster)

```bash
cd filegroup && docker compose up -d --build       # node1 :8443, node2 :8444, node3 :8445
cd ../clients/csharp && dotnet run --project DemoLocal -- http://localhost:8443 http://localhost:8444
```

The second address reads the uploaded file back from a **different node**,
which succeeds only when Raft metadata and chunk replication are working.

## Testing

Run each block from the repository root.

```bash
# 1. C++ unit + integration tests (includes the 3-node cluster tests)
cd filegroup && mkdir -p build && cd build
cmake .. && make -j$(nproc) && ctest --output-on-failure     # 20 tests

# 2. C# client tests
cd clients/csharp && dotnet test FileGroup.slnx              # 12 tests

# 3. End-to-end replication across a real 3-node Docker cluster
cd filegroup
make up        # build + start node1 :8443, node2 :8444, node3 :8445
make verify    # upload to node1, read the file back from node2
make down      # stop
```

`ctest` runs 20 tests, including:
- `Raft` — includes a 3-voter election + replication case
- `ClusterReplication` — Raft metadata and chunks over real gRPC on localhost
- `ClusterEngine` — full 3-node upload → read back from a *different* node

`make verify` proves the same against the Docker cluster: a read from node2
succeeds only if both Raft metadata and chunk replication worked.

## What's Left

- Step 19 — background scrubbing (integrity checking) is still stubbed
- Production hardening: mTLS for cluster-internal RPCs, automatic
  re-replication after a node failure, structured logging

See `todo/REMAINING.md` for open items and `todo/COMPLETED.md` for what's done.
