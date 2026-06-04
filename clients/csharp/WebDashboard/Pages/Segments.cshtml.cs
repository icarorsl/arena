using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class SegmentsModel : PageModel
{
    private readonly Engine.EngineClient _client;

    public List<SegmentSummary> Segments { get; set; } = new();
    public string? Error { get; set; }

    public SegmentsModel(Engine.EngineClient client) => _client = client;

    public async Task OnGetAsync()
    {
        try
        {
            var resp = await _client.ListSegmentsAsync(new ListSegmentsRequest());
            Segments = resp.Segments.Select(s => new SegmentSummary
            {
                FileName = s.FileName,
                NodeId = s.NodeId,
                GroupId = s.GroupId,
                TableId = s.TableId,
                TotalSize = s.TotalSize,
                UsedBytes = s.UsedBytes,
                ChunkCount = s.ChunkCount,
                IsPage = s.IsPage,
                CreatedAtUs = s.CreatedAtUs
            }).OrderBy(s => s.NodeId).ThenBy(s => s.GroupId).ThenBy(s => s.TableId).ToList();
        }
        catch (Exception ex)
        {
            Error = $"Cannot reach engine: {ex.Message}";
        }
    }

    public class SegmentSummary
    {
        public string FileName { get; set; } = "";
        public uint NodeId { get; set; }
        public uint GroupId { get; set; }
        public uint TableId { get; set; }
        public ulong TotalSize { get; set; }
        public ulong UsedBytes { get; set; }
        public uint ChunkCount { get; set; }
        public bool IsPage { get; set; }
        public ulong CreatedAtUs { get; set; }
    }
}
