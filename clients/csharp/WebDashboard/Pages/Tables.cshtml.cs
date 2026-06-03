using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

[IgnoreAntiforgeryToken]
public class TablesModel : PageModel
{
    private readonly Engine.EngineClient _client;

    public List<TableSummary> Tables { get; set; } = new();
    public string? Error { get; set; }
    public bool Created { get; set; }

    [BindProperty] public uint TableId { get; set; }
    [BindProperty] public uint GroupId { get; set; } = 1;
    [BindProperty] public string TableName { get; set; } = "";

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
                Name = TableName
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
                Name = t.Name
            }).ToList();

            foreach (var t in Tables)
            {
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
    }
}
