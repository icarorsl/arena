using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class UploadModel : PageModel
{
    private readonly Engine.EngineClient _client;
    private const int ChunkSize = 65536; // 64KB default

    [BindProperty]
    public IFormFile? UploadedFile { get; set; }

    [BindProperty]
    public uint GroupId { get; set; } = 1;

    [BindProperty]
    public uint TableId { get; set; } = 1;

    public bool Done { get; set; }
    public ulong UploadedFileId { get; set; }
    public uint UploadedVersion { get; set; }
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
            // 1. Open session
            var totalSize = (ulong)UploadedFile.Length;
            var expectedChunks = (uint)Math.Ceiling((double)totalSize / ChunkSize);

            var session = await _client.OpenSessionAsync(new OpenSessionRequest
            {
                GroupId = GroupId,
                TableId = TableId,
                TotalSize = totalSize,
                ExpectedChunks = expectedChunks
            });

            // 2. Write chunks
            using var stream = UploadedFile.OpenReadStream();
            var buffer = new byte[ChunkSize];
            uint chunkIdx = 0;

            while (true)
            {
                int bytesRead = await stream.ReadAsync(buffer, 0, ChunkSize);
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
        }
        catch (Exception ex)
        {
            Error = $"Upload failed: {ex.Message}";
        }

        return Page();
    }
}
