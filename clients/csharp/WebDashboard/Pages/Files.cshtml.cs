using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Eng = Filegroup.Engine;

namespace WebDashboard.Pages;

public class FilesModel : PageModel
{
    private readonly Engine.EngineClient _client;

    public List<Eng.FileInfo> Files { get; set; } = new();
    public List<TableRef> Tables { get; set; } = new();
    public uint SelectedGroup { get; set; }
    public uint SelectedTable { get; set; }
    public string? Error { get; set; }

    public record TableRef(uint TableId, uint GroupId, string Name);

    public FilesModel(Engine.EngineClient client) => _client = client;

    public async Task OnGetAsync([FromQuery] uint group = 0, [FromQuery] uint table = 0)
    {
        SelectedGroup = group;
        SelectedTable = table;
        try
        {
            var tableResp = await _client.GetTablesAsync(new GetTablesRequest());
            Tables = tableResp.Tables
                .Select(t => new TableRef(t.TableId, t.GroupId, t.Name))
                .OrderBy(t => t.GroupId).ThenBy(t => t.TableId)
                .ToList();

            // Default to first table with files if none selected
            if (SelectedGroup == 0 || SelectedTable == 0)
            {
                if (Tables.Count > 0)
                {
                    SelectedGroup = Tables[0].GroupId;
                    SelectedTable = Tables[0].TableId;
                }
                else
                {
                    SelectedGroup = 1;
                    SelectedTable = 1;
                }
            }

            var resp = await _client.ListFilesAsync(new ListFilesRequest
            {
                GroupId = SelectedGroup,
                TableId = SelectedTable,
                PageSize = 500
            });
            Files = resp.Files.ToList();
        }
        catch (Exception ex)
        {
            Error = $"Cannot reach engine: {ex.Message}";
        }
    }
}
