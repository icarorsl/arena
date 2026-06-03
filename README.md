# Arena — Distributed File Storage

Arena is a high-performance distributed file storage system built in C++ with a C# gRPC client SDK.

| Component | Dir | Language | Status |
|---|---|---|---|
| Engine | `filegroup/` | C++20 | ✅ Phase 1 (Steps 1–13, 20–22) |
| C# Client | `clients/csharp/` | C# / .NET 8 | ✅ 12/12 tests passing |
| Docker | `filegroup/docker-compose.yml` | Docker | ✅ End-to-end working |

## Quick Start

```bash
cd filegroup && docker compose up -d --build
cd ../clients/csharp && dotnet run --project DemoLocal
```

## What's Left (Phase 2)

Steps 14–19: heartbeat, expiry scanner, compaction, recovery, scrubbing — currently stubbed.

See `todo/REMAINING.md` for details.
