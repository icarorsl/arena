using Microsoft.AspNetCore.Mvc.RazorPages;

namespace WebDashboard.Pages;

public class PlayModel : PageModel
{
    public string? Error { get; set; }

    public void OnGet(long id)
    {
        // The video element in the view will fetch the raw bytes.
        // If needed, we could validate the file exists here.
    }
}
