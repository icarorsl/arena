using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

[IgnoreAntiforgeryToken]
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
    public string? Note { get; set; }

    public UploadModel(Engine.EngineClient client) => _client = client;

    [BindProperty]
    public ulong ExistingFileId { get; set; }
    public uint PreselectedTable { get; set; }
    public List<(uint TableId, uint GroupId, string Name)> Tables { get; set; } = new();

    public async Task OnGetAsync([FromQuery] ulong fileId = 0, [FromQuery] uint table = 0)
    {
        ExistingFileId = fileId;
        PreselectedTable = table;
        try
        {
            var resp = await _client.GetTablesAsync(new GetTablesRequest());
            Tables = resp.Tables.Select(t => (t.TableId, t.GroupId, t.Name)).OrderBy(t => t.TableId).ToList();
            if (PreselectedTable == 0 && Tables.Count > 0) PreselectedTable = Tables[0].TableId;
        }
        catch { /* use defaults */ }
    }

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
                LogicalFileId = ExistingFileId,
                TotalSize = totalSize,
                ExpectedChunks = 0
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

            if (!complete.Success)
            {
                Error = string.IsNullOrEmpty(complete.Error) ? "Complete session failed." : complete.Error;
                return Page();
            }

            if (ExistingFileId > 0)
            {
                return RedirectToPage("/File", new { id = ExistingFileId });
            }

            Done = true;
            UploadedFileId = complete.LogicalFileId;
            UploadedVersion = complete.VersionNumber;
            UploadedSize = UploadedFile.Length;
            if (!string.IsNullOrEmpty(complete.Error))
            {
                Note = complete.Error;
            }
        }
        catch (Exception ex)
        {
            Error = $"Upload failed: {ex.Message}";
        }

        return Page();
    }
}
