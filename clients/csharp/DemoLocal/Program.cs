using Filegroup.Engine;
using Grpc.Net.Client;
using Grpc.Core;

// ============================================================================
// FILE Group — Local/Docker Demo (no mTLS)
//
// Usage:
//   dotnet run --project DemoLocal
//   dotnet run --project DemoLocal -- http://localhost:8443
// ============================================================================

var address = args.FirstOrDefault(a => a.StartsWith("http")) ?? "http://localhost:8443";
const string ApiKey = "test-api-key";
const uint GroupId = 1, TableId = 1;

Console.WriteLine($"=== FILE Group Local Demo ===\nAddress: {address}\n");

var channel = GrpcChannel.ForAddress(address);
var client = new Engine.EngineClient(channel);
var headers = new Metadata { { "x-api-key", ApiKey } };

// Upload
Console.WriteLine("[1] Uploading...");
var data = new byte[4096];
for (int i = 0; i < data.Length; i++) data[i] = (byte)(i % 256);

var session = await client.OpenSessionAsync(new OpenSessionRequest
    { GroupId = GroupId, TableId = TableId, TotalSize = (ulong)data.Length, ExpectedChunks = 1 }, headers);
Console.WriteLine($"  Session: {session.SessionId}");

await client.WriteChunkAsync(new WriteChunkRequest
    { SessionId = session.SessionId, ChunkIndex = 0, Data = Google.Protobuf.ByteString.CopyFrom(data) }, headers);

var complete = await client.CompleteSessionAsync(new CompleteSessionRequest
    { SessionId = session.SessionId }, headers);
Console.WriteLine($"  Complete: file_id={complete.FileId} v{complete.VersionNumber}\n");

// Read
Console.WriteLine("[2] Reading...");
using var read = client.ReadFile(new ReadFileRequest { LogicalFileId = complete.LogicalFileId }, headers);
var all = new List<byte>();
while (await read.ResponseStream.MoveNext(CancellationToken.None))
    all.AddRange(read.ResponseStream.Current.Data);
Console.WriteLine($"  Got {all.Count} bytes — match: {all.SequenceEqual(data)}\n");

// List
Console.WriteLine("[3] Listing...");
var files = await client.ListFilesAsync(new ListFilesRequest
    { GroupId = GroupId, TableId = TableId, PageSize = 10 }, headers);
Console.WriteLine($"  {files.Files.Count} file(s)\n");

// Delete
Console.WriteLine("[4] Deleting...");
await client.DeleteFileAsync(new DeleteFileRequest { LogicalFileId = complete.LogicalFileId }, headers);
Console.WriteLine("  Done.\n");

Console.WriteLine("=== OK ===");
