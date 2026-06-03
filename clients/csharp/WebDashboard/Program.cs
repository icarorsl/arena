using Filegroup.Engine;
using Grpc.Net.Client;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddRazorPages();

// Register gRPC client — connect to engine (Docker or native)
var engineAddress = builder.Configuration.GetValue<string>("Engine:Address") ?? "http://localhost:8443";
builder.Services.AddSingleton(sp =>
{
    var channel = GrpcChannel.ForAddress(engineAddress);
    return new Engine.EngineClient(channel);
});

var app = builder.Build();

if (!app.Environment.IsDevelopment())
    app.UseExceptionHandler("/Error");

app.UseStaticFiles();
app.UseRouting();
app.MapRazorPages();
app.Run();
