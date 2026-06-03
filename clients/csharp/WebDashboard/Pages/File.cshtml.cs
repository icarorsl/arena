using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Eng = Filegroup.Engine;

namespace WebDashboard.Pages;

public class FileModel : PageModel
{
    private readonly Engine.EngineClient _client;

    public Eng.FileInfo? FileInfo { get; set; }
    public List<VersionInfo> Versions { get; set; } = new();
    public string? Error { get; set; }
    public bool Deleted { get; set; }

    public FileModel(Engine.EngineClient client) => _client = client;

    public async Task OnGetAsync(long id)
    {
        try
        {
            var info = await _client.GetFileInfoAsync(new GetFileInfoRequest
            {
                LogicalFileId = (ulong)id
            });

            if (info.File == null)
            {
                Error = "File not found.";
                return;
            }

            FileInfo = info.File;

            var vers = await _client.ListVersionsAsync(new ListVersionsRequest
            {
                LogicalFileId = (ulong)id
            });

            Versions = vers.Versions.OrderByDescending(v => v.VersionNumber).ToList();
        }
        catch (Exception ex)
        {
            Error = $"Cannot reach engine: {ex.Message}";
        }
    }

    public async Task<IActionResult> OnPostDeleteAsync(long id)
    {
        try
        {
            await _client.DeleteFileAsync(new DeleteFileRequest { LogicalFileId = (ulong)id });
            Deleted = true;
        }
        catch (Exception ex)
        {
            Error = $"Delete failed: {ex.Message}";
        }
        return Page();
    }

    public async Task<IActionResult> OnGetDownloadAsync(long id)
    {
        try
        {
            using var call = _client.ReadFile(new ReadFileRequest { LogicalFileId = (ulong)id });
            var data = new List<byte>();
            while (await call.ResponseStream.MoveNext(CancellationToken.None))
                data.AddRange(call.ResponseStream.Current.Data);

            return File(data.ToArray(), "application/octet-stream");
        }
        catch
        {
            return NotFound();
        }
    }
}
