using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Eng = Filegroup.Engine;

namespace WebDashboard.Pages;

[IgnoreAntiforgeryToken]
public class FileModel : PageModel
{
    private readonly Engine.EngineClient _client;

    public Eng.FileInfo? FileInfo { get; set; }
    public List<VersionInfo> Versions { get; set; } = new();
    public string? Error { get; set; }
    public bool CanAddVersion { get; set; }
    public uint MaxVersions { get; set; }

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

            // Fetch table config to determine max_versions
            try
            {
                var tables = await _client.GetTablesAsync(new GetTablesRequest());
                var table = tables.Tables.FirstOrDefault(t => t.TableId == FileInfo.TableId);
                MaxVersions = table?.MaxVersions ?? 0;
                uint liveCount = (uint)Versions.Count(v => v.State == Eng.VersionState.VersionComplete || v.State == Eng.VersionState.VersionMarkedDeleted);
                CanAddVersion = MaxVersions == 0 || liveCount < MaxVersions;
            }
            catch { CanAddVersion = true; }
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
            return RedirectToPage(new { id });
        }
        catch (Exception ex)
        {
            Error = $"Delete failed: {ex.Message}";
            return Page();
        }
    }

    public async Task<IActionResult> OnPostDeleteVersionAsync(long id, uint version)
    {
        try
        {
            await _client.DeleteVersionAsync(new DeleteVersionRequest
            {
                LogicalFileId = (ulong)id,
                VersionNumber = version
            });
        }
        catch (Exception ex)
        {
            Error = $"Delete version failed: {ex.Message}";
        }
        return RedirectToPage(new { id });
    }

    public async Task<IActionResult> OnGetDownloadAsync(long id, [FromQuery] uint version = 0)
    {
        try
        {
            using var call = _client.ReadFile(new ReadFileRequest { LogicalFileId = (ulong)id, VersionNumber = version });

            Response.ContentType = "application/octet-stream";
            Response.Headers["Accept-Ranges"] = "bytes";

            // Stream bytes directly: gRPC → HTTP, no buffering
            var total = 0L;
            while (await call.ResponseStream.MoveNext(HttpContext.RequestAborted))
            {
                var chunk = call.ResponseStream.Current.Data;
                await Response.Body.WriteAsync(chunk.Memory, HttpContext.RequestAborted);
                await Response.Body.FlushAsync(HttpContext.RequestAborted);
                total += chunk.Length;
            }

            return new EmptyResult();
        }
        catch (OperationCanceledException)
        {
            return new EmptyResult(); // client disconnected — ok
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[download] lid={id} v={version}: {ex.GetType().Name}: {ex.Message}");
            if (ex.InnerException != null)
                Console.Error.WriteLine($"[download] inner: {ex.InnerException.GetType().Name}: {ex.InnerException.Message}");
            return NotFound();
        }
    }
}
