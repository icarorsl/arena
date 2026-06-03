#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>

#include "engine.grpc.pb.h"
#include "config/config.h"
#include "engine/engine_service.h"
#include "metrics/metrics.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

namespace {

// ============================================================================
// Engine gRPC Service — wraps EngineServer
// ============================================================================

class EngineGrpcService final : public filegroup::engine::Engine::Service {
public:
    explicit EngineGrpcService(filegroup::EngineServer& server) : server_(server) {}

    grpc::Status OpenSession(ServerContext* ctx,
                              const filegroup::engine::OpenSessionRequest* req,
                              filegroup::engine::OpenSessionResponse* resp) override
    {
        (void)ctx;
        auto result = server_.open_session(
            req->group_id(), req->table_id(),
            req->logical_file_id(), req->total_size(),
            req->expected_chunks(), req->file_expires_in_days());

        resp->set_session_id(result.session_id);
        resp->set_file_id(result.file_id);
        resp->set_logical_file_id(result.logical_file_id);
        resp->set_version_number(result.version_number);
        resp->set_resolved_chunk_size(result.resolved_chunk_size);
        resp->set_encryption(static_cast<filegroup::engine::EncryptionAlgo>(result.encryption));
        if (!result.error.empty()) resp->set_error(result.error);
        return result.success ? Status::OK : Status(grpc::INTERNAL, result.error);
    }

    grpc::Status WriteChunk(ServerContext* ctx,
                             const filegroup::engine::WriteChunkRequest* req,
                             filegroup::engine::WriteChunkResponse* resp) override
    {
        (void)ctx;
        std::vector<uint8_t> data(req->data().begin(), req->data().end());
        auto result = server_.write_chunk(req->session_id(), req->chunk_index(), data);

        resp->set_success(result.success);
        resp->set_already_confirmed(result.already_confirmed);
        if (!result.error.empty()) resp->set_error(result.error);
        return result.success ? Status::OK : Status(grpc::INTERNAL, result.error);
    }

    grpc::Status CompleteSession(ServerContext* ctx,
                                  const filegroup::engine::CompleteSessionRequest* req,
                                  filegroup::engine::CompleteSessionResponse* resp) override
    {
        (void)ctx;
        auto result = server_.complete_session(req->session_id(), req->content_checksum());

        resp->set_success(result.success);
        resp->set_logical_file_id(result.logical_file_id);
        resp->set_file_id(result.file_id);
        resp->set_version_number(result.version_number);
        if (!result.error.empty()) resp->set_error(result.error);
        return result.success ? Status::OK : Status(grpc::INTERNAL, result.error);
    }

    grpc::Status ResumeSession(ServerContext* ctx,
                                const filegroup::engine::ResumeSessionRequest* req,
                                filegroup::engine::ResumeSessionResponse* resp) override
    {
        (void)ctx;
        auto result = server_.resume_session(req->session_id());

        resp->set_session_id(result.session_id);
        resp->set_file_id(result.file_id);
        resp->set_logical_file_id(result.logical_file_id);
        resp->set_version_number(result.version_number);
        resp->set_resolved_chunk_size(result.resolved_chunk_size);
        resp->set_encryption(static_cast<filegroup::engine::EncryptionAlgo>(result.encryption));
        for (auto& c : result.confirmed_chunks) resp->add_confirmed_chunks(c);
        if (!result.error.empty()) resp->set_error(result.error);
        return result.success ? Status::OK : Status(grpc::INTERNAL, result.error);
    }

    grpc::Status DeleteFile(ServerContext* ctx,
                             const filegroup::engine::DeleteFileRequest* req,
                             filegroup::engine::DeleteFileResponse* resp) override
    {
        (void)ctx;
        auto result = server_.delete_file(req->logical_file_id());
        resp->set_success(result.success);
        if (!result.error.empty()) resp->set_error(result.error);
        return result.success ? Status::OK : Status(grpc::INTERNAL, result.error);
    }

    grpc::Status DeleteVersion(ServerContext* ctx,
                                const filegroup::engine::DeleteVersionRequest* req,
                                filegroup::engine::DeleteVersionResponse* resp) override
    {
        (void)ctx;
        auto result = server_.delete_version(req->logical_file_id(), req->version_number());
        resp->set_success(result.success);
        if (!result.error.empty()) resp->set_error(result.error);
        return result.success ? Status::OK : Status(grpc::INTERNAL, result.error);
    }

    grpc::Status GetFileInfo(ServerContext* ctx,
                              const filegroup::engine::GetFileInfoRequest* req,
                              filegroup::engine::GetFileInfoResponse* resp) override
    {
        (void)ctx;
        auto result = server_.get_file_info(req->logical_file_id());
        if (!result.success) return Status(grpc::NOT_FOUND, result.error);

        auto* f = resp->mutable_file();
        f->set_logical_file_id(result.logical_file_id);
        f->set_table_id(result.table_id);
        f->set_group_id(result.group_id);
        f->set_latest_version(result.latest_version);
        f->set_state(static_cast<filegroup::engine::FileState>(result.state));
        f->set_total_size(result.total_size);
        f->set_created_at_us(result.created_at_us);
        return Status::OK;
    }

    grpc::Status ListFiles(ServerContext* ctx,
                            const filegroup::engine::ListFilesRequest* req,
                            filegroup::engine::ListFilesResponse* resp) override
    {
        (void)ctx;
        auto files = server_.list_files(req->group_id(), req->table_id());
        for (auto& f : files) {
            auto* fi = resp->add_files();
            fi->set_logical_file_id(f.logical_file_id);
            fi->set_table_id(f.table_id);
            fi->set_group_id(f.group_id);
            fi->set_latest_version(f.latest_version);
            fi->set_state(static_cast<filegroup::engine::FileState>(f.state));
            fi->set_total_size(f.total_size);
            fi->set_created_at_us(f.created_at_us);
        }
        return Status::OK;
    }

    grpc::Status ListVersions(ServerContext* ctx,
                               const filegroup::engine::ListVersionsRequest* req,
                               filegroup::engine::ListVersionsResponse* resp) override
    {
        (void)ctx;
        auto versions = server_.list_versions(req->logical_file_id());
        for (auto& v : versions) {
            auto* vi = resp->add_versions();
            vi->set_file_id(v.file_id);
            vi->set_version_number(v.version_number);
            vi->set_state(static_cast<filegroup::engine::VersionState>(v.state));
            vi->set_total_size(v.total_size);
            vi->set_chunk_count(v.chunk_count);
            vi->set_expires_at_us(v.expires_at_us);
            vi->set_encryption(static_cast<filegroup::engine::EncryptionAlgo>(v.encryption));
            vi->set_created_at_us(v.created_at_us);
        }
        return Status::OK;
    }

    grpc::Status CancelSession(ServerContext* ctx,
                                const filegroup::engine::CancelSessionRequest* req,
                                filegroup::engine::CancelSessionResponse* resp) override
    {
        (void)ctx;
        auto result = server_.cancel_session(req->session_id());
        resp->set_success(result.success);
        if (!result.error.empty()) resp->set_error(result.error);
        return result.success ? Status::OK : Status(grpc::INTERNAL, result.error);
    }

    // ── Table Management ────────────────────────────────────────────────

    grpc::Status CreateTable(ServerContext* ctx,
                              const filegroup::engine::CreateTableRequest* req,
                              filegroup::engine::CreateTableResponse* resp) override
    {
        (void)ctx;
        auto result = server_.create_table(req->table_id(), req->group_id(), req->name(), req->file_expires_in_days(), req->max_versions());
        resp->set_success(result.success);
        if (!result.error.empty()) resp->set_error(result.error);
        return result.success ? Status::OK : Status(grpc::INTERNAL, result.error);
    }

    grpc::Status GetTables(ServerContext* ctx,
                            const filegroup::engine::GetTablesRequest* req,
                            filegroup::engine::GetTablesResponse* resp) override
    {
        (void)ctx; (void)req;
        auto tables = server_.get_tables();
        for (auto& t : tables) {
            auto* ti = resp->add_tables();
            ti->set_table_id(t.table_id);
            ti->set_group_id(t.group_id);
            ti->set_name(t.name);
            ti->set_chunk_size(t.chunk_size);
            ti->set_replication_factor(t.replication_factor);
            ti->set_encryption(static_cast<filegroup::engine::EncryptionAlgo>(t.encryption));
            ti->set_max_versions(t.max_versions);
            ti->set_file_expires_in_days(t.file_expires_in_days);
        }
        return Status::OK;
    }

    // ReadFile is server-streaming — simplified for Phase 1
    grpc::Status ReadFile(ServerContext* ctx,
                           const filegroup::engine::ReadFileRequest* req,
                           grpc::ServerWriter<filegroup::engine::ReadFileResponse>* writer) override
    {
        (void)ctx;
        auto result = server_.read_file(req->logical_file_id(), req->version_number());
        if (!result.data.empty()) {
            filegroup::engine::ReadFileResponse chunk;
            chunk.set_data(result.data.data(), result.data.size());
            chunk.set_is_last_chunk(true);
            writer->Write(chunk);
        } else if (!result.error.empty()) {
            filegroup::engine::ReadFileResponse chunk;
            chunk.set_error(result.error);
            writer->Write(chunk);
        }
        return Status::OK;
    }

    grpc::Status ReadChunk(ServerContext* ctx,
                            const filegroup::engine::ReadChunkRequest* req,
                            filegroup::engine::ReadChunkResponse* resp) override
    {
        (void)ctx;
        auto result = server_.read_chunk(req->logical_file_id(), req->version_number(), req->chunk_index());
        if (!result.data.empty()) {
            resp->set_data(result.data.data(), result.data.size());
        } else if (!result.error.empty()) {
            resp->set_error(result.error);
        }
        return Status::OK;
    }

private:
    filegroup::EngineServer& server_;
};

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    std::cout << "=== FILE Group Engine (Phase 1 + gRPC) ===\n";

    // ── Build config programmatically (TOML parser is incomplete for arrays) ──
    filegroup::ClusterConfig config;

    config.tls.ca_cert_file = "certs/ca.crt";
    config.tls.engine_cert_file = "certs/engine.crt";
    config.tls.engine_key_file = "certs/engine.key";

    filegroup::RegistryNodeConfig reg;
    reg.id = 1;
    reg.address = "localhost:50051";
    reg.cert_file = "certs/ca.crt";
    reg.key_file = "certs/engine.key";
    config.registry_nodes.push_back(reg);

    filegroup::StorageNodeConfig storage;
    storage.node_id = 1;
    storage.address = "localhost:5001";
    storage.role = filegroup::NodeRole::ORIGIN;
    storage.cert_file = "certs/engine.crt";
    storage.key_file = "certs/engine.key";
    config.storage_nodes.push_back(storage);

    filegroup::FileGroupConfig group;
    group.group_id = 1;
    group.name = "default";
    group.chunk_size = 65536;
    group.min_chunk_bytes = 1024;
    group.replication_factor = 1;
    group.max_versions = 5;
    group.file_expires_in_days = 0;
    group.expiry_granularity = filegroup::ExpiryGranularity::UNSET;
    group.encryption = filegroup::EncryptionAlgo::NONE;
    config.groups.push_back(group);

    // No default table — users must create tables explicitly via the dashboard.
    // This avoids ghost files appearing from Raft replay of table-less SESSION_OPEN entries.

    filegroup::ApiKeyConfig api_key;
    api_key.key = "test-api-key";
    api_key.name = "development";
    api_key.groups = {1};
    api_key.permissions = {"read", "write", "admin"};
    config.api_keys.push_back(api_key);

    // ── Start metrics HTTP server ──────────────────────────────────────
    filegroup::MetricsServer metrics(9090);
    metrics.start();

    // ── Start engine ─────────────────────────────────────────────────────
    const char* data_dir = std::getenv("FILEGROUP_DATA_DIR");
    filegroup::EngineServer engine_server(config, &metrics, data_dir ? data_dir : "/var/lib/filegroup");

    // ── Start gRPC server ────────────────────────────────────────────────
    std::string server_address("0.0.0.0:8443");
    EngineGrpcService service(engine_server);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Engine listening on " << server_address << "\n";
    std::cout << "Press Ctrl+C to stop.\n";

    server->Wait();
    return 0;
}
