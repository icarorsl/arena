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
                uint completeCount = (uint)Versions.Count(v => v.State == Eng.VersionState.VersionComplete);
                CanAddVersion = MaxVersions == 0 || completeCount < MaxVersions;
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
            // Get file info for size and chunk count
            var info = await _client.GetFileInfoAsync(new GetFileInfoRequest { LogicalFileId = (ulong)id });
            if (info?.File == null) return NotFound();
            var totalSize = (long)info.File.TotalSize;

            Response.ContentType = "application/octet-stream";
            Response.Headers["Accept-Ranges"] = "bytes";

            // Parse Range header for seeking
            long start = 0, end = totalSize - 1;
            var rangeHeader = Request.Headers.Range.ToString();
            if (!string.IsNullOrEmpty(rangeHeader) && rangeHeader.StartsWith("bytes="))
            {
                var range = rangeHeader[6..].Split('-');
                start = long.TryParse(range[0], out var s) ? s : 0;
                end = range.Length > 1 && long.TryParse(range[1], out var e) ? Math.Min(e, totalSize - 1) : totalSize - 1;
                Response.StatusCode = 206;
                Response.Headers["Content-Range"] = $"bytes {start}-{end}/{totalSize}";
            }

            // Calculate which chunks to read
            var chunkSize = (long)(info.LatestVersion != null && info.LatestVersion.ChunkCount > 0
                ? info.File.TotalSize / info.LatestVersion.ChunkCount
                : 65536);
            if (chunkSize == 0) chunkSize = 65536;
            var firstChunk = (uint)(start / chunkSize);
            var lastChunk = (uint)(end / chunkSize);

            using var call = _client.ReadFile(new ReadFileRequest { LogicalFileId = (ulong)id, VersionNumber = version });

            uint chunkIdx = 0;
            long bytesWritten = 0;
            var totalRange = end - start + 1;

            while (await call.ResponseStream.MoveNext(HttpContext.RequestAborted))
            {
                var resp = call.ResponseStream.Current;
                if (resp.IsLastChunk && resp.Data.IsEmpty) break;
                if (!string.IsNullOrEmpty(resp.Error)) return NotFound();

                var data = resp.Data;
                if (chunkIdx >= firstChunk && chunkIdx <= lastChunk)
                {
                    var chunkStart = (long)chunkIdx * chunkSize;
                    var offset = start > chunkStart ? (int)(start - chunkStart) : 0;
                    var len = (int)Math.Min(data.Length - offset, totalRange - bytesWritten);
                    if (len > 0 && offset < data.Length)
                    {
                        await Response.Body.WriteAsync(data.Memory.Slice(offset, len), HttpContext.RequestAborted);
                        bytesWritten += len;
                    }
                }
                chunkIdx++;
                if (bytesWritten >= totalRange) break;
            }

            return new EmptyResult();
        }
        catch (OperationCanceledException) { return new EmptyResult(); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[download] lid={id} v={version}: {ex.GetType().Name}: {ex.Message}");
            return NotFound();
        }
    }
}
