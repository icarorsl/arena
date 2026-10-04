using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class StatusModel : PageModel
{
    private readonly EngineConnection _connection;

    public bool EngineOk { get; set; }
    public uint FileCount { get; set; }
    public uint TableCount { get; set; }
    public string? Error { get; set; }
    public string ActiveAddress => _connection.ActiveAddress;
    public List<NodeStatus> Nodes { get; set; } = new();

    public StatusModel(EngineConnection connection) => _connection = connection;

    public async Task OnGetAsync()
    {
        // Query every configured node directly so we can show the whole cluster.
        foreach (var address in _connection.Nodes)
        {
            var node = new NodeStatus { Address = address };
            try
            {
                var status = await _connection.ClientFor(address).GetNodeStatusAsync(
                    new GetNodeStatusRequest(),
                    deadline: DateTime.UtcNow.AddSeconds(2));
                node.Alive = true;
                node.NodeId = status.NodeId;
                node.IsLeader = status.IsLeader;
                node.LeaderId = status.LeaderId;
                node.CommitIndex = status.CommitIndex;
                node.LastApplied = status.LastApplied;
            }
            catch (Exception ex)
            {
                node.Alive = false;
                node.Error = ex.Message;
            }
            Nodes.Add(node);
        }

        // Overall counts from whichever node is currently active.
        try
        {
            var files = await _connection.Client.ListFilesAsync(new ListFilesRequest
            {
                GroupId = 0, TableId = 0, PageSize = 1000
            });
            FileCount = (uint)files.Files.Count;

            var tables = await _connection.Client.GetTablesAsync(new GetTablesRequest());
            TableCount = (uint)tables.Tables.Count;
            EngineOk = true;
        }
        catch (Exception ex)
        {
            Error = $"Engine unreachable: {ex.Message}";
        }
    }

    public class NodeStatus
    {
        public string Address { get; set; } = "";
        public bool Alive { get; set; }
        public uint NodeId { get; set; }
        public bool IsLeader { get; set; }
        public uint LeaderId { get; set; }
        public ulong CommitIndex { get; set; }
        public ulong LastApplied { get; set; }
        public string? Error { get; set; }
    }
}
