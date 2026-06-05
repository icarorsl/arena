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
            bool isRange = Request.Headers.Range.ToString().StartsWith("bytes=");

            if (isRange)
            {
                // Get file size from version info
                var vers = await _client.ListVersionsAsync(new ListVersionsRequest { LogicalFileId = (ulong)id });
                var vi = version > 0
                    ? vers.Versions.FirstOrDefault(v => v.VersionNumber == version)
                    : vers.Versions.OrderByDescending(v => v.VersionNumber).FirstOrDefault();
                if (vi == null || vi.TotalSize == 0) return NotFound();
                long totalSize = (long)vi.TotalSize;

                // Parse Range header
                var rh = Request.Headers.Range.ToString();
                var p = rh[6..].Split('-');
                long start = long.TryParse(p[0], out var s) ? s : 0;
                long end = p.Length > 1 && long.TryParse(p[1], out var e) ? Math.Min(e, totalSize - 1) : totalSize - 1;
                if (start > end || start >= totalSize) return BadRequest();
                ulong length = (ulong)(end - start + 1);

                Console.Error.WriteLine($"[download] RANGE: file={id} v={version} bytes={start}-{end}/{totalSize} len={length}");

                // Fetch only the requested byte range from the engine
                var sw = System.Diagnostics.Stopwatch.StartNew();
                var rangeResp = await _client.ReadRangeAsync(new ReadRangeRequest
                {
                    LogicalFileId = (ulong)id,
                    VersionNumber = version,
                    OffsetBytes = (ulong)start,
                    LengthBytes = length
                });
                sw.Stop();
                Console.Error.WriteLine($"[download] ReadRangeAsync took {sw.ElapsedMilliseconds}ms, got {rangeResp.Data.Length} bytes");

                if (!string.IsNullOrEmpty(rangeResp.Error)) return NotFound();

                var data = rangeResp.Data.ToByteArray();
                if (data.Length == 0) return NotFound();

                // Detect MIME from first bytes if range starts at 0
                if (start == 0 && data.Length >= 12)
                {
                    Response.ContentType = DetectMimeType(data.AsSpan(0, Math.Min(12, data.Length)));
                }
                else
                {
                    // For non-zero start ranges, sniff magic bytes from file header
                    try
                    {
                        var sniffResp = await _client.ReadRangeAsync(new ReadRangeRequest
                        {
                            LogicalFileId = (ulong)id,
                            VersionNumber = version,
                            OffsetBytes = 0,
                            LengthBytes = 12
                        });
                        if (string.IsNullOrEmpty(sniffResp.Error) && sniffResp.Data.Length >= 4)
                            Response.ContentType = DetectMimeType(sniffResp.Data.Span);
                        else
                            Response.ContentType = "application/octet-stream";
                    }
                    catch { Response.ContentType = "application/octet-stream"; }
                }

                Response.Headers["Accept-Ranges"] = "bytes";
                Response.StatusCode = 206;
                Response.Headers["Content-Range"] = $"bytes {start}-{start + data.Length - 1}/{totalSize}";

                await Response.Body.WriteAsync(data);
            }
            else
            {
                Console.Error.WriteLine($"[download] FULL: file={id} v={version} — streaming via ReadFile");
                var sw = System.Diagnostics.Stopwatch.StartNew();
                using var call = _client.ReadFile(new ReadFileRequest { LogicalFileId = (ulong)id, VersionNumber = version });

                // Stream immediately — set MIME from first chunk, flush rest
                var contentTypeSet = false;
                long totalBytes = 0;
                while (await call.ResponseStream.MoveNext(HttpContext.RequestAborted))
                {
                    var resp = call.ResponseStream.Current;
                    if (!string.IsNullOrEmpty(resp.Error)) return NotFound();

                    if (!contentTypeSet && resp.Data.Length >= 12)
                    {
                        Response.ContentType = DetectMimeType(resp.Data.Span);
                        Response.Headers["Accept-Ranges"] = "bytes";
                        contentTypeSet = true;
                    }

                    totalBytes += resp.Data.Length;
                    await Response.Body.WriteAsync(resp.Data.Memory, HttpContext.RequestAborted);
                    await Response.Body.FlushAsync(HttpContext.RequestAborted);
                }
                sw.Stop();
                Console.Error.WriteLine($"[download] ReadFile stream done: {totalBytes} bytes in {sw.ElapsedMilliseconds}ms");
            }
        }
        catch (OperationCanceledException) { }
        return new EmptyResult();
    }

    private static string DetectMimeType(ReadOnlySpan<byte> data)
    {
        // WebM: \x1a\x45\xdf\xa3
        if (data.Length >= 4 && data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3)
            return "video/webm";
        // MP4/MOV: ftyp at offset 4
        if (data.Length >= 12 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p')
            return "video/mp4";
        // AVI: RIFF....AVI
        if (data.Length >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F'
            && data[8] == 'A' && data[9] == 'V' && data[10] == 'I')
            return "video/avi";
        // QuickTime MOV
        if (data.Length >= 8 && data[4] == 'm' && data[5] == 'o' && data[6] == 'o' && data[7] == 'v')
            return "video/quicktime";
        // Matroska MKV
        if (data.Length >= 4 && data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3)
            return "video/x-matroska";
        // JPEG
        if (data.Length >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
            return "image/jpeg";
        // PNG
        if (data.Length >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
            return "image/png";
        // GIF
        if (data.Length >= 4 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8')
            return "image/gif";
        // PDF
        if (data.Length >= 4 && data[0] == '%' && data[1] == 'P' && data[2] == 'D' && data[3] == 'F')
            return "application/pdf";
        return "application/octet-stream";
    }
}
