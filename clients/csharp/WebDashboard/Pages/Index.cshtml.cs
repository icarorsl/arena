using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class IndexModel : PageModel
{
    private readonly Engine.EngineClient _client;
    private readonly IConfiguration _config;

    public string EngineAddress { get; set; } = "";
    public bool EngineOk { get; set; }

    public IndexModel(Engine.EngineClient client, IConfiguration config)
    {
        _client = client;
        _config = config;
        EngineAddress = _config.GetValue<string>("Engine:Address") ?? "http://localhost:8443";
    }

    public async Task OnGetAsync()
    {
        try
        {
            // Quick health check: try listing files
            await _client.ListFilesAsync(new ListFilesRequest
            {
                GroupId = 1,
                TableId = 1,
                PageSize = 1
            });
            EngineOk = true;
        }
        catch
        {
            EngineOk = false;
        }
    }
}
