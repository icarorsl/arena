using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class TablesModel : PageModel
{
    private readonly Engine.EngineClient _client;

    public List<TableSummary> Tables { get; set; } = new();
    public string? Error { get; set; }

    public TablesModel(Engine.EngineClient client) => _client = client;

    public async Task OnGetAsync()
    {
        try
        {
            // Query all tables we know about (Phase 1: group 1)
            var files = await _client.ListFilesAsync(new ListFilesRequest
            {
                GroupId = 1,
                TableId = 1,
                PageSize = 1000
            });

            var byTable = new Dictionary<(uint group, uint table), TableSummary>();

            foreach (var f in files.Files)
            {
                var key = (f.GroupId, f.TableId);
                if (!byTable.ContainsKey(key))
                    byTable[key] = new TableSummary { GroupId = f.GroupId, TableId = f.TableId };

                var t = byTable[key];
                t.FileCount++;
                t.TotalSize += f.TotalSize;
                if (f.State == Filegroup.Engine.FileState.FileActive) t.ActiveCount++;
                else t.DeletedCount++;
            }

            Tables = byTable.Values.OrderBy(t => t.TableId).ToList();
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
        public uint FileCount { get; set; }
        public uint ActiveCount { get; set; }
        public uint DeletedCount { get; set; }
        public ulong TotalSize { get; set; }
    }
}
