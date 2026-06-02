using Filegroup.Engine;
using Xunit;

namespace FileGroup.Tests;

public class ProtoTypeTests
{
    [Fact]
    public void OpenSessionRequest_DefaultValues()
    {
        var req = new OpenSessionRequest();
        Assert.Equal(0u, req.GroupId);
        Assert.Equal(0u, req.TableId);
    }

    [Fact]
    public void OpenSessionRequest_CanSetAllFields()
    {
        var req = new OpenSessionRequest
        {
            GroupId = 10,
            TableId = 5,
            LogicalFileId = 42,
            TotalSize = 1048576,
            ExpectedChunks = 16,
            FileExpiresInDays = 30
        };

        Assert.Equal(10u, req.GroupId);
        Assert.Equal(5u, req.TableId);
        Assert.Equal(42uL, req.LogicalFileId);
        Assert.Equal(1048576uL, req.TotalSize);
        Assert.Equal(16u, req.ExpectedChunks);
        Assert.Equal(30u, req.FileExpiresInDays);
    }

    [Fact]
    public void WriteChunkRequest_RoundTrip()
    {
        var data = new byte[] { 0x01, 0x02, 0x03, 0xFF };
        var req = new WriteChunkRequest
        {
            SessionId = 7,
            ChunkIndex = 3,
            Data = Google.Protobuf.ByteString.CopyFrom(data)
        };

        Assert.Equal(7uL, req.SessionId);
        Assert.Equal(3u, req.ChunkIndex);
        Assert.Equal(data, req.Data.ToByteArray());
    }

    [Fact]
    public void CompleteSessionRequest_SetsChecksum()
    {
        var req = new CompleteSessionRequest
        {
            SessionId = 5,
            ContentChecksum = 0xDEADBEEF
        };

        Assert.Equal(5uL, req.SessionId);
        Assert.Equal(0xDEADBEEFu, req.ContentChecksum);
    }

    [Fact]
    public void ReadFileRequest_LatestVersion()
    {
        var req = new ReadFileRequest
        {
            LogicalFileId = 100,
            VersionNumber = 0 // 0 = latest
        };

        Assert.Equal(100uL, req.LogicalFileId);
        Assert.Equal(0u, req.VersionNumber);
    }

    [Fact]
    public void DeleteFileRequest_Simple()
    {
        var req = new DeleteFileRequest { LogicalFileId = 999 };
        Assert.Equal(999uL, req.LogicalFileId);
    }

    [Fact]
    public void ResumeSessionRequest_Simple()
    {
        var req = new ResumeSessionRequest { SessionId = 42 };
        Assert.Equal(42uL, req.SessionId);
    }

    [Fact]
    public void ListFilesRequest_Pagination()
    {
        var req = new ListFilesRequest
        {
            GroupId = 1,
            TableId = 2,
            PageSize = 25,
            PageToken = "next-page"
        };

        Assert.Equal(1u, req.GroupId);
        Assert.Equal(2u, req.TableId);
        Assert.Equal(25u, req.PageSize);
        Assert.Equal("next-page", req.PageToken);
    }

    [Fact]
    public void GetFileInfoRequest_Simple()
    {
        var req = new GetFileInfoRequest { LogicalFileId = 1 };
        Assert.Equal(1uL, req.LogicalFileId);
    }

    [Fact]
    public void FileInfoResponse_AllFields()
    {
        var info = new Filegroup.Engine.FileInfo
        {
            LogicalFileId = 100,
            TableId = 5,
            GroupId = 10,
            LatestVersion = 3,
            State = FileState.FileActive,
            TotalSize = 65536,
            CreatedAtUs = 1234567890
        };

        Assert.Equal(100uL, info.LogicalFileId);
        Assert.Equal(5u, info.TableId);
        Assert.Equal(10u, info.GroupId);
        Assert.Equal(3u, info.LatestVersion);
        Assert.Equal(FileState.FileActive, info.State);
    }

    [Fact]
    public void VersionInfo_States()
    {
        var info = new VersionInfo
        {
            FileId = 200,
            VersionNumber = 1,
            State = VersionState.VersionComplete,
            TotalSize = 1024,
            ChunkCount = 4,
            Encryption = EncryptionAlgo.EncryptionNone
        };

        Assert.Equal(VersionState.VersionComplete, info.State);
        Assert.Equal(EncryptionAlgo.EncryptionNone, info.Encryption);
        Assert.Equal(4u, info.ChunkCount);
    }

    [Fact]
    public void CancelSessionRequest_Simple()
    {
        var req = new CancelSessionRequest { SessionId = 99 };
        Assert.Equal(99uL, req.SessionId);
    }
}

