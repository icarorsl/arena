using Filegroup.Engine;
using Google.Protobuf;
using Grpc.Core;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class ReplicationModel : PageModel
{
    private readonly EngineConnection _connection;

    public ReplicationModel(EngineConnection connection) => _connection = connection;

    public bool Ran { get; set; }
    public string? Error { get; set; }
    public string WriterAddress { get; set; } = "";
    public ulong LogicalFileId { get; set; }
    public int PayloadSize { get; set; }
    public List<NodeResult> Results { get; set; } = new();

    public void OnGet() { }

    public async Task OnPostAsync()
    {
        Ran = true;
        var nodes = _connection.Nodes;
        if (nodes.Count == 0)
        {
            Error = "No engine nodes configured.";
            return;
        }

        // Use the failover client so the probe still runs when the first node
        // is down; the view reports whichever node actually served the write.
        var writer = _connection.Client;

        try
        {
            // The table must exist before an upload; ignore "already exists".
            try
            {
                await writer.CreateTableAsync(
                    new CreateTableRequest { TableId = 1, GroupId = 1, Name = "default" },
                    deadline: DateTime.UtcNow.AddSeconds(5));
            }
            catch (RpcException) { }

            PayloadSize = 4096;
            var payload = new byte[PayloadSize];
            for (int i = 0; i < payload.Length; i++) payload[i] = (byte)((i * 31 + 7) % 256);

            var session = await writer.OpenSessionAsync(new OpenSessionRequest
            {
                GroupId = 1, TableId = 1,
                TotalSize = (ulong)payload.Length, ExpectedChunks = 1
            }, deadline: DateTime.UtcNow.AddSeconds(10));

            await writer.WriteChunkAsync(new WriteChunkRequest
            {
                SessionId = session.SessionId,
                ChunkIndex = 0,
                Data = ByteString.CopyFrom(payload)
            }, deadline: DateTime.UtcNow.AddSeconds(30));

            var complete = await writer.CompleteSessionAsync(
                new CompleteSessionRequest { SessionId = session.SessionId },
                deadline: DateTime.UtcNow.AddSeconds(30));
            LogicalFileId = complete.LogicalFileId;
            WriterAddress = _connection.ActiveAddress;

            // Read the file back from every node. Followers may need a moment to
            // apply the Raft commit, so retry briefly per node.
            foreach (var address in nodes)
            {
                var result = new NodeResult { Address = address };
                var client = _connection.ClientFor(address);
                for (int attempt = 1; attempt <= 12; attempt++)
                {
                    try
                    {
                        var bytes = new List<byte>();
                        using var call = client.ReadFile(
                            new ReadFileRequest { LogicalFileId = LogicalFileId },
                            deadline: DateTime.UtcNow.AddSeconds(10));
                        while (await call.ResponseStream.MoveNext(CancellationToken.None))
                            bytes.AddRange(call.ResponseStream.Current.Data);

                        result.Bytes = bytes.Count;
                        if (bytes.Count == payload.Length && bytes.SequenceEqual(payload))
                        {
                            result.Ok = true;
                            break;
                        }
                        result.Error = bytes.Count == 0 ? "no data yet" : "size/checksum mismatch";
                    }
                    catch (Exception ex)
                    {
                        result.Error = ex.Message;
                    }

                    if (attempt < 12) await Task.Delay(250);
                }
                Results.Add(result);
            }

            // Best-effort cleanup of the probe file.
            try
            {
                await writer.DeleteFileAsync(new DeleteFileRequest { LogicalFileId = LogicalFileId },
                    deadline: DateTime.UtcNow.AddSeconds(10));
            }
            catch (RpcException) { }
        }
        catch (Exception ex)
        {
            Error = ex.Message;
        }
    }

    public class NodeResult
    {
        public string Address { get; set; } = "";
        public bool Ok { get; set; }
        public int Bytes { get; set; }
        public string? Error { get; set; }
    }
}
