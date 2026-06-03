using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class UploadModel : PageModel
{
    private readonly Engine.EngineClient _client;

    [BindProperty]
    public IFormFile? UploadedFile { get; set; }

    [BindProperty]
    public uint GroupId { get; set; } = 1;

    [BindProperty]
    public uint TableId { get; set; } = 1;

    public bool Done { get; set; }
    public ulong UploadedFileId { get; set; }
    public uint UploadedVersion { get; set; }
    public long UploadedSize { get; set; }
    public string? Error { get; set; }

    public UploadModel(Engine.EngineClient client) => _client = client;

    public void OnGet() { }

    public async Task<IActionResult> OnPostAsync()
    {
        if (UploadedFile == null || UploadedFile.Length == 0)
        {
            Error = "No file selected.";
            return Page();
        }

        try
        {
            // 1. Open session — engine tells us the chunk size
            var totalSize = (ulong)UploadedFile.Length;

            var session = await _client.OpenSessionAsync(new OpenSessionRequest
            {
                GroupId = GroupId,
                TableId = TableId,
                TotalSize = totalSize,
                ExpectedChunks = 0  // let engine compute
            });

            var chunkSize = (int)(session.ResolvedChunkSize > 0 ? session.ResolvedChunkSize : 65536);

            // 2. Write chunks using engine's resolved chunk size
            using var stream = UploadedFile.OpenReadStream();
            var buffer = new byte[chunkSize];
            uint chunkIdx = 0;

            while (true)
            {
                int bytesRead = await stream.ReadAsync(buffer, 0, chunkSize);
                if (bytesRead == 0) break;

                var data = Google.Protobuf.ByteString.CopyFrom(buffer, 0, bytesRead);
                await _client.WriteChunkAsync(new WriteChunkRequest
                {
                    SessionId = session.SessionId,
                    ChunkIndex = chunkIdx,
                    Data = data
                });

                chunkIdx++;
            }

            // 3. Complete
            var complete = await _client.CompleteSessionAsync(new CompleteSessionRequest
            {
                SessionId = session.SessionId
            });

            Done = true;
            UploadedFileId = complete.LogicalFileId;
            UploadedVersion = complete.VersionNumber;
            UploadedSize = UploadedFile.Length;
        }
        catch (Exception ex)
        {
            Error = $"Upload failed: {ex.Message}";
        }

        return Page();
    }
}
