using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class StatusModel : PageModel
{
    private readonly Engine.EngineClient _client;

    public bool EngineOk { get; set; }
    public uint FileCount { get; set; }
    public uint TableCount { get; set; }
    public List<NodeStatus> Nodes { get; set; } = new();
    public string? Error { get; set; }

    public StatusModel(Engine.EngineClient client) => _client = client;

    public async Task OnGetAsync()
    {
        try
        {
            var files = await _client.ListFilesAsync(new ListFilesRequest
            {
                GroupId = 1, TableId = 1, PageSize = 1000
            });
            FileCount = (uint)files.Files.Count;
            EngineOk = true;

            var tables = await _client.GetTablesAsync(new GetTablesRequest());
            TableCount = (uint)tables.Tables.Count;
        }
        catch (Exception ex)
        {
            Error = $"Engine unreachable: {ex.Message}";
            return;
        }

        Nodes.Add(new NodeStatus
        {
            NodeId = 1,
            Alive = EngineOk,
            LastCheck = DateTime.Now
        });
    }

    public class NodeStatus
    {
        public uint NodeId { get; set; }
        public bool Alive { get; set; }
        public DateTime LastCheck { get; set; }
    }
}
