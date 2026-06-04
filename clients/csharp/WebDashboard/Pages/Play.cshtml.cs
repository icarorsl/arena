using Filegroup.Engine;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class PlayModel : PageModel
{
    private readonly Engine.EngineClient _client;
    public string? Error { get; set; }
    public uint TableId { get; set; }
    public uint GroupId { get; set; }
    public uint VersionNumber { get; set; }
    public string? VersionState { get; set; }

    public PlayModel(Engine.EngineClient client) => _client = client;

    public async Task OnGetAsync(long id, [FromQuery] uint version = 0)
    {
        try
        {
            var resp = await _client.GetFileInfoAsync(new GetFileInfoRequest { LogicalFileId = (ulong)id });
            GroupId = resp.File?.GroupId ?? 0;
            TableId = resp.File?.TableId ?? 0;

            var vers = await _client.ListVersionsAsync(new ListVersionsRequest { LogicalFileId = (ulong)id });
            VersionInfo? vi;
            if (version > 0)
                vi = vers.Versions.FirstOrDefault(v => v.VersionNumber == version);
            else
                vi = vers.Versions.OrderByDescending(v => v.VersionNumber).FirstOrDefault();

            VersionNumber = vi?.VersionNumber ?? 0;
            VersionState = vi?.State.ToString();
        }
        catch (Exception ex)
        {
            Error = ex.Message;
        }
    }
}
