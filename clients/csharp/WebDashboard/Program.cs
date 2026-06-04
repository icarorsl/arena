using Filegroup.Engine;
using Grpc.Net.Client;
using Microsoft.AspNetCore.Http.Features;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddRazorPages();

// Allow large file uploads (up to 4 GB)
builder.WebHost.ConfigureKestrel(options =>
{
    options.Limits.MaxRequestBodySize = null; // unlimited
});
builder.Services.Configure<FormOptions>(options =>
{
    options.MultipartBodyLengthLimit = 4L * 1024 * 1024 * 1024; // 4 GB
});

// Register gRPC client — connect to engine (Docker or native)
var engineAddress = builder.Configuration.GetValue<string>("Engine:Address") ?? "http://localhost:8443";
builder.Services.AddSingleton(sp =>
{
    var channel = GrpcChannel.ForAddress(engineAddress, new GrpcChannelOptions
    {
        MaxReceiveMessageSize = null, // unlimited — engine sends entire file as single message
        MaxSendMessageSize = 256 * 1024 * 1024
    });
    return new Engine.EngineClient(channel);
});

var app = builder.Build();

// Disable browser caching during development
app.Use(async (context, next) =>
{
    context.Response.Headers["Cache-Control"] = "no-cache, no-store, must-revalidate";
    context.Response.Headers["Pragma"] = "no-cache";
    context.Response.Headers["Expires"] = "0";
    await next();
});

if (!app.Environment.IsDevelopment())
    app.UseExceptionHandler("/Error");

app.UseStaticFiles();
app.UseRouting();
app.MapRazorPages();
app.Run();
