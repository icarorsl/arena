using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Eng = Filegroup.Engine;

namespace WebDashboard.Pages;

public class FilesModel : PageModel
{
    private readonly Engine.EngineClient _client;

    public List<Eng.FileInfo> Files { get; set; } = new();
    public string? Error { get; set; }

    public FilesModel(Engine.EngineClient client) => _client = client;

    public async Task OnGetAsync([FromQuery] uint group = 1, [FromQuery] uint table = 1)
    {
        try
        {
            var resp = await _client.ListFilesAsync(new ListFilesRequest
            {
                GroupId = group,
                TableId = table,
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
