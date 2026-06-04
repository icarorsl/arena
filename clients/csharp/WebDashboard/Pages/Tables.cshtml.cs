using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

[IgnoreAntiforgeryToken]
public class TablesModel : PageModel
{
    private readonly Engine.EngineClient _client;

    public List<TableSummary> Tables { get; set; } = new();
    public List<SegmentEntry> AllSegments { get; set; } = new();
    public string? Error { get; set; }
    public bool Created { get; set; }

    [BindProperty] public uint TableId { get; set; }
    [BindProperty] public uint GroupId { get; set; } = 1;
    [BindProperty] public string TableName { get; set; } = "";
    [BindProperty] public ulong ChunkSize { get; set; }
    [BindProperty] public uint ReplicationFactor { get; set; }
    [BindProperty] public uint ExpiresInDays { get; set; }
    [BindProperty] public uint MaxVersions { get; set; }

    public TablesModel(Engine.EngineClient client) => _client = client;

    public async Task OnGetAsync() => await LoadTables();

    public async Task<IActionResult> OnPostAsync()
    {
        try
        {
            await _client.CreateTableAsync(new CreateTableRequest
            {
                TableId = TableId,
                GroupId = GroupId,
                Name = TableName,
                ChunkSize = ChunkSize,
                ReplicationFactor = ReplicationFactor,
                FileExpiresInDays = ExpiresInDays,
                MaxVersions = MaxVersions
            });
            Created = true;
        }
        catch (Exception ex)
        {
            Error = $"Create failed: {ex.Message}";
        }
        await LoadTables();
        return Page();
    }

    private async Task LoadTables()
    {
        try
        {
            var resp = await _client.GetTablesAsync(new GetTablesRequest());
            Tables = resp.Tables.Select(t => new TableSummary
            {
                TableId = t.TableId,
                GroupId = t.GroupId,
                Name = t.Name,
                ChunkSize = t.ChunkSize,
                ReplicationFactor = t.ReplicationFactor,
                MaxVersions = t.MaxVersions,
                ExpiresInDays = t.FileExpiresInDays
            }).ToList();

            foreach (var t in Tables)
            {
                if (t.GroupId == 0 || t.TableId == 0) continue;
                var files = await _client.ListFilesAsync(new ListFilesRequest
                {
                    GroupId = t.GroupId,
                    TableId = t.TableId,
                    PageSize = 1000
                });
                t.FileCount = (uint)files.Files.Count;
                foreach (var f in files.Files)
                {
                    t.TotalSize += f.TotalSize;
                    if (f.State == Filegroup.Engine.FileState.FileActive) t.ActiveCount++;
                    else t.DeletedCount++;
                }
            }

            // Load all segments once
            try
            {
                var segResp = await _client.ListSegmentsAsync(new ListSegmentsRequest());
                AllSegments = segResp.Segments.Select(s => new SegmentEntry
                {
                    FileName = s.FileName,
                    NodeId = s.NodeId,
                    GroupId = s.GroupId,
                    TableId = s.TableId,
                    ChunkCount = s.ChunkCount,
                    UsedBytes = s.UsedBytes,
                    TotalSize = s.TotalSize,
                    IsPage = s.IsPage,
                    CreatedAtUs = s.CreatedAtUs
                }).ToList();
            }
            catch { /* segments are optional, don't fail the page */ }
        }
        catch (Exception ex)
        {
            Error = $"Cannot reach engine: {ex.Message}";
        }
    }

    public class TableSummary
    {
        public uint TableId { get; set; }
        public uint GroupId { get; set; }
        public string Name { get; set; } = "";
        public uint FileCount { get; set; }
        public uint ActiveCount { get; set; }
        public uint DeletedCount { get; set; }
        public ulong TotalSize { get; set; }
        public ulong ChunkSize { get; set; }
        public uint ReplicationFactor { get; set; }
        public uint MaxVersions { get; set; }
        public uint ExpiresInDays { get; set; }
    }

    public class SegmentEntry
    {
        public string FileName { get; set; } = "";
        public uint NodeId { get; set; }
        public uint GroupId { get; set; }
        public uint TableId { get; set; }
        public uint ChunkCount { get; set; }
        public ulong UsedBytes { get; set; }
        public ulong TotalSize { get; set; }
        public bool IsPage { get; set; }
        public ulong CreatedAtUs { get; set; }
    }
}
