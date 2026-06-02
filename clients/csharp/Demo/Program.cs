using FileGroup;
using Filegroup.Engine;

// ============================================================================
// FILE Group — C# Demo App
//
// Demonstrates the full upload → read → list → delete pipeline.
// Requires a running engine server (see README for startup instructions).
//
// Data path (on the C++ server):
//   /tmp/filegroup/node_1/seg_1_<group>_<table>_<seq>.seg
// ============================================================================

// ── Configuration ────────────────────────────────────────────────────────────
const string EngineAddress = "https://localhost:8443";
const string CertPath      = "certs/client.crt";
const string KeyPath       = "certs/client.key";
const string CaPath        = "certs/ca.crt";
const string ApiKey        = "test-api-key";

const uint GroupId  = 1;
const uint TableId  = 1;

Console.WriteLine("=== FILE Group C# Demo ===\n");

// ── 1. Connect ──────────────────────────────────────────────────────────────
Console.WriteLine("[1] Connecting to engine...");

using var client = new FileGroupClient(
    EngineAddress, CertPath, KeyPath, CaPath, ApiKey);

Console.WriteLine("    Connected.\n");

// ── 2. Upload a file ────────────────────────────────────────────────────────
Console.WriteLine("[2] Uploading file...");

var fileData = new byte[4096];
for (int i = 0; i < fileData.Length; i++) fileData[i] = (byte)(i % 256);
uint expectedChunks = 1;

var session = await client.OpenSessionAsync(
    GroupId, TableId,
    totalSize: (ulong)fileData.Length,
    expectedChunks: expectedChunks);

Console.WriteLine($"    Session opened: {session.SessionId}");
Console.WriteLine($"    Chunk size: {session.ResolvedChunkSize}");
Console.WriteLine($"    Encryption: {session.Encryption}");

await client.WriteChunkAsync(session.SessionId, chunkIndex: 0, fileData);
Console.WriteLine("    Chunk 0 written.");

var complete = await client.CompleteSessionAsync(session.SessionId);
Console.WriteLine($"    Upload complete: file_id={complete.FileId}, version={complete.VersionNumber}\n");

// ── 3. Read it back ─────────────────────────────────────────────────────────
Console.WriteLine("[3] Reading file back...");

var readData = await client.ReadFileAsync(complete.LogicalFileId);

bool match = readData.Length == fileData.Length
          && readData.SequenceEqual(fileData);
Console.WriteLine($"    Read {readData.Length} bytes — match: {match}\n");

// ── 4. List files ───────────────────────────────────────────────────────────
Console.WriteLine("[4] Listing files...");

var files = await client.ListFilesAsync(GroupId, TableId, pageSize: 10);
foreach (var f in files)
{
    Console.WriteLine($"    logical_file_id={f.LogicalFileId}  "
                    + $"table={f.TableId}  latest_version={f.LatestVersion}  "
                    + $"state={f.State}");
}
Console.WriteLine($"    Total: {files.Count} file(s)\n");

// ── 5. List versions ────────────────────────────────────────────────────────
Console.WriteLine("[5] Listing versions...");

var versions = await client.ListVersionsAsync(complete.LogicalFileId);
foreach (var v in versions)
{
    Console.WriteLine($"    file_id={v.FileId}  version={v.VersionNumber}  "
                    + $"state={v.State}  size={v.TotalSize}  chunks={v.ChunkCount}");
}
Console.WriteLine($"    Total: {versions.Count} version(s)\n");

// ── 6. File info ────────────────────────────────────────────────────────────
Console.WriteLine("[6] File info...");

var info = await client.GetFileInfoAsync(complete.LogicalFileId);
Console.WriteLine($"    logical_file_id={info.File.LogicalFileId}  "
                + $"latest={info.File.LatestVersion}  state={info.File.State}\n");

// ── 7. Delete file ──────────────────────────────────────────────────────────
Console.WriteLine("[7] Deleting file...");

await client.DeleteFileAsync(complete.LogicalFileId);
Console.WriteLine("    Deleted.\n");

Console.WriteLine("=== Demo complete ===");
