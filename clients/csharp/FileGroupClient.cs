using System.Net.Security;
using System.Security.Cryptography.X509Certificates;
using Grpc.Core;
using Grpc.Net.Client;
using Filegroup.Engine;

namespace FileGroup;

/// <summary>
/// Client for FILE Group distributed object store.
/// Connects via gRPC + mTLS + API key authentication.
///
/// Usage:
///   using var client = new FileGroupClient(
///       "https://engine:8443",
///       "certs/client.crt",
///       "certs/client.key",
///       "certs/ca.crt",
///       "your-api-key");
///
///   var session = await client.OpenSessionAsync(groupId: 1, tableId: 5, fileSize: 1048576);
///   await client.WriteChunkAsync(session.SessionId, chunkIndex: 0, data);
///   await client.CompleteSessionAsync(session.SessionId);
///
///   var file = await client.ReadFileAsync(logicalFileId: 1);
/// </summary>
public class FileGroupClient : IDisposable
{
    private readonly GrpcChannel _channel;
    private readonly Engine.EngineClient _grpc;
    private readonly Metadata _authHeaders;

    /// <summary>
    /// Create a client connected to a FILE Group engine.
    /// </summary>
    /// <param name="engineAddress">Engine URL, e.g. "https://engine.example.com:8443"</param>
    /// <param name="clientCertPath">Path to client certificate PEM file</param>
    /// <param name="clientKeyPath">Path to client private key PEM file</param>
    /// <param name="caCertPath">Path to cluster CA certificate for server validation</param>
    /// <param name="apiKey">API key from cluster config</param>
    public FileGroupClient(
        string engineAddress,
        string clientCertPath,
        string clientKeyPath,
        string caCertPath,
        string apiKey)
    {
        var cert = X509Certificate2.CreateFromPemFile(clientCertPath, clientKeyPath);

        var handler = new SocketsHttpHandler
        {
            SslOptions = new SslClientAuthenticationOptions
            {
                ClientCertificates = new X509Certificate2Collection(cert),
                RemoteCertificateValidationCallback = (sender, certificate, chain, errors) =>
                {
                    if (certificate is null) return false;
                    chain!.ChainPolicy.CustomTrustStore.Add(new X509Certificate2(caCertPath));
                    chain.ChainPolicy.TrustMode = X509ChainTrustMode.CustomRootTrust;
                    chain.ChainPolicy.VerificationFlags = X509VerificationFlags.AllowUnknownCertificateAuthority;
                    return chain.Build((X509Certificate2)certificate);
                }
            }
        };

        _channel = GrpcChannel.ForAddress(engineAddress, new GrpcChannelOptions
        {
            HttpHandler = handler
        });

        _grpc = new Engine.EngineClient(_channel);
        _authHeaders = new Metadata { { "x-api-key", apiKey } };
    }

    // ========================================================================
    // Upload
    // ========================================================================

    /// <summary>Open a new upload session.</summary>
    public async Task<OpenSessionResponse> OpenSessionAsync(
        uint groupId,
        uint tableId,
        ulong logicalFileId = 0,
        ulong totalSize = 0,
        uint expectedChunks = 0,
        uint fileExpiresInDays = 0,
        CancellationToken ct = default)
    {
        return await _grpc.OpenSessionAsync(new OpenSessionRequest
        {
            GroupId = groupId,
            TableId = tableId,
            LogicalFileId = logicalFileId,
            TotalSize = totalSize,
            ExpectedChunks = expectedChunks,
            FileExpiresInDays = fileExpiresInDays
        }, _authHeaders, cancellationToken: ct);
    }

    /// <summary>Write a single chunk. Idempotent — safe to retry.</summary>
    public async Task<WriteChunkResponse> WriteChunkAsync(
        ulong sessionId,
        uint chunkIndex,
        byte[] data,
        CancellationToken ct = default)
    {
        return await _grpc.WriteChunkAsync(new WriteChunkRequest
        {
            SessionId = sessionId,
            ChunkIndex = chunkIndex,
            Data = Google.Protobuf.ByteString.CopyFrom(data)
        }, _authHeaders, cancellationToken: ct);
    }

    /// <summary>Complete an upload session — file becomes readable.</summary>
    public async Task<CompleteSessionResponse> CompleteSessionAsync(
        ulong sessionId,
        uint contentChecksum = 0,
        CancellationToken ct = default)
    {
        return await _grpc.CompleteSessionAsync(new CompleteSessionRequest
        {
            SessionId = sessionId,
            ContentChecksum = contentChecksum
        }, _authHeaders, cancellationToken: ct);
    }

    /// <summary>Resume an interrupted upload session.</summary>
    public async Task<ResumeSessionResponse> ResumeSessionAsync(
        ulong sessionId,
        CancellationToken ct = default)
    {
        return await _grpc.ResumeSessionAsync(new ResumeSessionRequest
        {
            SessionId = sessionId
        }, _authHeaders, cancellationToken: ct);
    }

    // ========================================================================
    // Read
    // ========================================================================

    /// <summary>Read an entire file (streaming). Returns all chunks reassembled.</summary>
    public async Task<byte[]> ReadFileAsync(
        ulong logicalFileId,
        uint versionNumber = 0,
        CancellationToken ct = default)
    {
        var request = new ReadFileRequest
        {
            LogicalFileId = logicalFileId,
            VersionNumber = versionNumber
        };

        using var call = _grpc.ReadFile(request, _authHeaders);
        using var ms = new MemoryStream();

        await foreach (var response in call.ResponseStream.ReadAllAsync(ct))
        {
            if (!string.IsNullOrEmpty(response.Error))
                throw new IOException($"Read failed: {response.Error}");

            ms.Write(response.Data.Span);
        }

        return ms.ToArray();
    }

    /// <summary>Read a single chunk.</summary>
    public async Task<byte[]> ReadChunkAsync(
        ulong logicalFileId,
        uint versionNumber,
        uint chunkIndex,
        CancellationToken ct = default)
    {
        var response = await _grpc.ReadChunkAsync(new ReadChunkRequest
        {
            LogicalFileId = logicalFileId,
            VersionNumber = versionNumber,
            ChunkIndex = chunkIndex
        }, _authHeaders, cancellationToken: ct);

        if (!string.IsNullOrEmpty(response.Error))
            throw new IOException($"Read chunk failed: {response.Error}");

        return response.Data.ToByteArray();
    }

    // ========================================================================
    // Management
    // ========================================================================

    /// <summary>Delete a logical file and all its versions.</summary>
    public async Task DeleteFileAsync(ulong logicalFileId, CancellationToken ct = default) =>
        await _grpc.DeleteFileAsync(new DeleteFileRequest { LogicalFileId = logicalFileId },
            _authHeaders, cancellationToken: ct);

    /// <summary>Delete a specific version of a file.</summary>
    public async Task DeleteVersionAsync(ulong logicalFileId, uint versionNumber,
        CancellationToken ct = default) =>
        await _grpc.DeleteVersionAsync(new DeleteVersionRequest
        {
            LogicalFileId = logicalFileId,
            VersionNumber = versionNumber
        }, _authHeaders, cancellationToken: ct);

    /// <summary>List files in a group/table.</summary>
    public async Task<List<Filegroup.Engine.FileInfo>> ListFilesAsync(
        uint groupId,
        uint tableId,
        uint pageSize = 50,
        CancellationToken ct = default)
    {
        var result = new List<Filegroup.Engine.FileInfo>();
        string? pageToken = null;

        do
        {
            var response = await _grpc.ListFilesAsync(new ListFilesRequest
            {
                GroupId = groupId,
                TableId = tableId,
                PageSize = pageSize,
                PageToken = pageToken ?? ""
            }, _authHeaders, cancellationToken: ct);

            if (!string.IsNullOrEmpty(response.Error))
                throw new IOException($"List files failed: {response.Error}");

            result.AddRange(response.Files);
            pageToken = string.IsNullOrEmpty(response.NextPageToken) ? null : response.NextPageToken;
        } while (pageToken != null);

        return result;
    }

    /// <summary>List all versions of a file.</summary>
    public async Task<List<Filegroup.Engine.VersionInfo>> ListVersionsAsync(
        ulong logicalFileId, CancellationToken ct = default)
    {
        var response = await _grpc.ListVersionsAsync(new ListVersionsRequest
        {
            LogicalFileId = logicalFileId
        }, _authHeaders, cancellationToken: ct);

        if (!string.IsNullOrEmpty(response.Error))
            throw new IOException($"List versions failed: {response.Error}");

        return new List<Filegroup.Engine.VersionInfo>(response.Versions);
    }

    /// <summary>Get file metadata.</summary>
    public async Task<GetFileInfoResponse> GetFileInfoAsync(
        ulong logicalFileId, CancellationToken ct = default) =>
        await _grpc.GetFileInfoAsync(new GetFileInfoRequest
        {
            LogicalFileId = logicalFileId
        }, _authHeaders, cancellationToken: ct);

    /// <summary>Cancel an in-progress upload session.</summary>
    public async Task CancelSessionAsync(ulong sessionId, CancellationToken ct = default) =>
        await _grpc.CancelSessionAsync(new CancelSessionRequest
        {
            SessionId = sessionId
        }, _authHeaders, cancellationToken: ct);

    // ========================================================================

    public void Dispose()
    {
        _channel.Dispose();
    }
}
