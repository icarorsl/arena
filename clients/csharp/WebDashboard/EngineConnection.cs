using Filegroup.Engine;
using Grpc.Core;
using Grpc.Net.Client;

namespace WebDashboard;

/// <summary>
/// Holds a channel to every configured engine node and exposes an
/// <see cref="Engine.EngineClient"/> that transparently fails over to the next
/// node when the active one is unreachable. Also lets pages address a specific
/// node (used by the Status and Replication pages).
/// </summary>
public sealed class EngineConnection
{
    public IReadOnlyList<string> Nodes { get; }

    /// Failover client — use this for normal page operations.
    public Engine.EngineClient Client { get; }

    public int ActiveIndex => Volatile.Read(ref _active);
    public string ActiveAddress => Nodes[Math.Clamp(ActiveIndex, 0, Nodes.Count - 1)];

    private readonly GrpcChannel[] _channels;
    private readonly Engine.EngineClient[] _perNode;
    private int _active;

    public EngineConnection(IConfiguration config)
    {
        var nodes = config.GetSection("Engine:Nodes").Get<string[]>();
        if (nodes is null || nodes.Length == 0)
        {
            var single = config.GetValue<string>("Engine:Address") ?? "http://localhost:8443";
            nodes = new[] { single };
        }
        Nodes = nodes;

        _channels = nodes.Select(CreateChannel).ToArray();
        _perNode = _channels.Select(ch => new Engine.EngineClient(ch)).ToArray();
        Client = new Engine.EngineClient(new FailoverCallInvoker(this));
    }

    /// A client bound to one specific node (no failover); falls back to the
    /// failover client if the address is unknown.
    public Engine.EngineClient ClientFor(string address)
    {
        for (int i = 0; i < Nodes.Count; i++)
        {
            if (string.Equals(Nodes[i], address, StringComparison.OrdinalIgnoreCase))
                return _perNode[i];
        }
        return Client;
    }

    internal int NodeCount => _channels.Length;
    internal CallInvoker InvokerAt(int index) => _channels[index].CreateCallInvoker();
    internal void Advance() => Volatile.Write(ref _active, (ActiveIndex + 1) % _channels.Length);

    private static GrpcChannel CreateChannel(string address) =>
        GrpcChannel.ForAddress(address, new GrpcChannelOptions
        {
            MaxReceiveMessageSize = null,          // engine may send a whole file in one message
            MaxSendMessageSize = 256 * 1024 * 1024
        });
}
