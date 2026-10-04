using Grpc.Core;

namespace WebDashboard;

/// <summary>
/// A <see cref="CallInvoker"/> that routes each call to the currently active
/// engine node and fails over to the next node when the active one is
/// unreachable. Grpc.Net.Client reports connection failures when the call is
/// awaited, so unary and server-streaming calls are wrapped so the retry can
/// happen at that point (streaming retries only before the first response, to
/// avoid duplicating data).
/// </summary>
internal sealed class FailoverCallInvoker : CallInvoker
{
    private readonly EngineConnection _connection;

    public FailoverCallInvoker(EngineConnection connection) => _connection = connection;

    public override TResponse BlockingUnaryCall<TRequest, TResponse>(
        Method<TRequest, TResponse> method, string? host, CallOptions options, TRequest request)
        => AsyncUnaryCall(method, host, options, request).ResponseAsync.GetAwaiter().GetResult();

    public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(
        Method<TRequest, TResponse> method, string? host, CallOptions options, TRequest request)
    {
        var response = new TaskCompletionSource<TResponse>(TaskCreationOptions.RunContinuationsAsynchronously);
        var headers = new TaskCompletionSource<Metadata>(TaskCreationOptions.RunContinuationsAsynchronously);
        AsyncUnaryCall<TResponse>? completed = null;

        _ = Task.Run(async () =>
        {
            RpcException? last = null;
            for (int attempt = 0; attempt < _connection.NodeCount; attempt++)
            {
                var call = _connection.InvokerAt(_connection.ActiveIndex)
                    .AsyncUnaryCall(method, host, options, request);
                try
                {
                    var responseAsync = await call.ResponseAsync.ConfigureAwait(false);
                    completed = call;
                    response.TrySetResult(responseAsync);
                    try { headers.TrySetResult(await call.ResponseHeadersAsync.ConfigureAwait(false)); }
                    catch { headers.TrySetResult(new Metadata()); }
                    return;
                }
                catch (RpcException ex) when (ex.StatusCode == StatusCode.Unavailable)
                {
                    last = ex;
                    call.Dispose();
                    _connection.Advance();
                }
                catch (Exception ex)
                {
                    response.TrySetException(ex);
                    headers.TrySetResult(new Metadata());
                    return;
                }
            }

            response.TrySetException(last ?? new RpcException(
                new Status(StatusCode.Unavailable, "no engine nodes reachable")));
            headers.TrySetResult(new Metadata());
        });

        return new AsyncUnaryCall<TResponse>(
            response.Task,
            headers.Task,
            () => completed?.GetStatus() ?? new Status(StatusCode.Unavailable, "pending"),
            () => completed?.GetTrailers() ?? new Metadata(),
            () => completed?.Dispose());
    }

    public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(
        Method<TRequest, TResponse> method, string? host, CallOptions options, TRequest request)
    {
        var stream = new FailoverStream<TResponse>(_connection,
            () => _connection.InvokerAt(_connection.ActiveIndex)
                .AsyncServerStreamingCall(method, host, options, request));

        return new AsyncServerStreamingCall<TResponse>(
            stream,
            stream.Headers,
            stream.GetStatus,
            stream.GetTrailers,
            stream.Dispose);
    }

    // Request-streaming calls cannot be replayed, so these go to the active node
    // without failover. (The engine API has no client-streaming methods.)
    public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(
        Method<TRequest, TResponse> method, string? host, CallOptions options)
        => _connection.InvokerAt(_connection.ActiveIndex).AsyncClientStreamingCall(method, host, options);

    public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(
        Method<TRequest, TResponse> method, string? host, CallOptions options)
        => _connection.InvokerAt(_connection.ActiveIndex).AsyncDuplexStreamingCall(method, host, options);

    /// <summary>Retrying reader for server-streaming calls.</summary>
    private sealed class FailoverStream<T> : IAsyncStreamReader<T>
    {
        private readonly EngineConnection _connection;
        private readonly Func<AsyncServerStreamingCall<T>> _start;
        private readonly TaskCompletionSource<Metadata> _headers =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        private AsyncServerStreamingCall<T>? _call;
        private IAsyncStreamReader<T>? _reader;
        private int _emitted;

        public FailoverStream(EngineConnection connection, Func<AsyncServerStreamingCall<T>> start)
        {
            _connection = connection;
            _start = start;
        }

        public Task<Metadata> Headers => _headers.Task;

        public T Current => _reader!.Current;

        public async Task<bool> MoveNext(CancellationToken cancellationToken)
        {
            while (true)
            {
                if (_reader is null)
                {
                    _call = _start();
                    _reader = _call.ResponseStream;
                    _ = _call.ResponseHeadersAsync.ContinueWith(t =>
                        _headers.TrySetResult(t.IsCompletedSuccessfully ? t.Result : new Metadata()),
                        TaskScheduler.Default);
                }

                try
                {
                    bool more = await _reader.MoveNext(cancellationToken).ConfigureAwait(false);
                    if (more) _emitted++;
                    return more;
                }
                catch (RpcException ex) when (ex.StatusCode == StatusCode.Unavailable && _emitted == 0)
                {
                    _call?.Dispose();
                    _reader = null;
                    _connection.Advance();
                }
            }
        }

        public Status GetStatus() => _call?.GetStatus() ?? new Status(StatusCode.Unavailable, "not started");
        public Metadata GetTrailers() => _call?.GetTrailers() ?? new Metadata();
        public void Dispose() => _call?.Dispose();
    }
}
