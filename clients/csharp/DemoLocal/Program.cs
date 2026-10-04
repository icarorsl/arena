using Filegroup.Engine;
using Grpc.Net.Client;
using Grpc.Core;

// ============================================================================
// FILE Group — Local/Docker Demo (no mTLS)
//
// Usage:
//   dotnet run --project DemoLocal
//   dotnet run --project DemoLocal -- http://localhost:8443
//   dotnet run --project DemoLocal -- <write-url> <read-url>   # cross-node check
// ============================================================================

var urls = args.Where(a => a.StartsWith("http")).ToArray();
var writeAddress = urls.FirstOrDefault() ?? "http://localhost:8443";
var readAddress = urls.Skip(1).FirstOrDefault() ?? writeAddress;
const string ApiKey = "test-api-key";
const uint GroupId = 1, TableId = 1;

Console.WriteLine($"=== FILE Group Local Demo ===\nWrite: {writeAddress}\nRead:  {readAddress}\n");

var channel = GrpcChannel.ForAddress(writeAddress);
var client = new Engine.EngineClient(channel);
var readClient = readAddress == writeAddress
    ? client
    : new Engine.EngineClient(GrpcChannel.ForAddress(readAddress));
var headers = new Metadata { { "x-api-key", ApiKey } };

// Tables must exist before an upload. Create it if missing (idempotent).
Console.WriteLine("[0] Ensuring table...");
var create = await client.CreateTableAsync(new CreateTableRequest
    { TableId = TableId, GroupId = GroupId, Name = "default" }, headers);
Console.WriteLine($"  table: success={create.Success} {create.Error}");

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

// Read (from the read node — a different node proves replication). A follower
// may not have caught up with the Raft commit yet, so retry briefly.
Console.WriteLine($"[2] Reading from {readAddress}...");
var all = new List<byte>();
for (int attempt = 1; attempt <= 20; attempt++)
{
    all.Clear();
    using var read = readClient.ReadFile(new ReadFileRequest { LogicalFileId = complete.LogicalFileId }, headers);
    while (await read.ResponseStream.MoveNext(CancellationToken.None))
        all.AddRange(read.ResponseStream.Current.Data);
    if (all.Count == data.Length && all.SequenceEqual(data)) break;
    if (attempt < 20) await Task.Delay(250);
}
Console.WriteLine($"  Got {all.Count} bytes — match: {all.SequenceEqual(data)}\n");

// List from the read node
Console.WriteLine("[3] Listing...");
var files = await readClient.ListFilesAsync(new ListFilesRequest
    { GroupId = GroupId, TableId = TableId, PageSize = 10 }, headers);
Console.WriteLine($"  {files.Files.Count} file(s)\n");

// Delete
Console.WriteLine("[4] Deleting...");
await readClient.DeleteFileAsync(new DeleteFileRequest { LogicalFileId = complete.LogicalFileId }, headers);
Console.WriteLine("  Done.\n");

Console.WriteLine("=== OK ===");
