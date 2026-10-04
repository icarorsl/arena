#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>

#include "engine.grpc.pb.h"
#include "cluster/cluster_node.h"
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

    // ── Storage Introspection ──────────────────────────────────────────

    grpc::Status ListSegments(ServerContext* ctx,
                               const filegroup::engine::ListSegmentsRequest* req,
                               filegroup::engine::ListSegmentsResponse* resp) override
    {
        (void)ctx; (void)req;
        auto segments = server_.list_segments();
        for (auto& s : segments) {
            auto* si = resp->add_segments();
            si->set_file_name(s.file_name);
            si->set_node_id(s.node_id);
            si->set_group_id(s.group_id);
            si->set_table_id(s.table_id);
            si->set_total_size(s.total_size);
            si->set_used_bytes(s.used_bytes);
            si->set_chunk_count(s.chunk_count);
            si->set_is_page(s.is_page);
            si->set_created_at_us(s.created_at_us);
        }
        return Status::OK;
    }

    // ── Cluster Introspection ──────────────────────────────────────────

    grpc::Status GetNodeStatus(ServerContext* ctx,
                               const filegroup::engine::GetNodeStatusRequest* req,
                               filegroup::engine::GetNodeStatusResponse* resp) override
    {
        (void)ctx; (void)req;
        auto s = server_.get_node_status();
        resp->set_node_id(s.node_id);
        resp->set_is_leader(s.is_leader);
        resp->set_leader_id(s.leader_id);
        resp->set_commit_index(s.commit_index);
        resp->set_last_applied(s.last_applied);
        if (!s.error.empty()) resp->set_error(s.error);
        return Status::OK;
    }

    // ReadFile is server-streaming — simplified for Phase 1
    grpc::Status ReadFile(ServerContext* ctx,
                           const filegroup::engine::ReadFileRequest* req,
                           grpc::ServerWriter<filegroup::engine::ReadFileResponse>* writer) override
    {
        (void)ctx;
        uint32_t ci = 0;
        bool ok = server_.read_file_stream(req->logical_file_id(), req->version_number(),
            [&](const uint8_t* data, size_t size) {
                filegroup::engine::ReadFileResponse chunk;
                chunk.set_data(data, size);
                chunk.set_chunk_index(ci++);
                writer->Write(chunk);
            });
        if (!ok) {
            filegroup::engine::ReadFileResponse err;
            err.set_error("not found");
            writer->Write(err);
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

    grpc::Status ReadRange(ServerContext* ctx,
                            const filegroup::engine::ReadRangeRequest* req,
                            filegroup::engine::ReadRangeResponse* resp) override
    {
        (void)ctx;
        auto result = server_.read_range(req->logical_file_id(), req->version_number(),
                                          req->offset_bytes(), req->length_bytes());
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

namespace {

std::vector<std::string> split_csv(const std::string& value) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= value.size()) {
        auto comma = value.find(',', start);
        auto end = (comma == std::string::npos) ? value.size() : comma;
        auto token = value.substr(start, end - start);
        if (!token.empty()) parts.push_back(token);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return parts;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    std::cout << "=== FILE Group Engine (Phase 1 + gRPC) ===\n";

    // ── Build config (TOML parser is incomplete for arrays, so allow env) ──
    filegroup::ClusterConfig config;

    config.tls.ca_cert_file = "certs/ca.crt";
    config.tls.engine_cert_file = "certs/engine.crt";
    config.tls.engine_key_file = "certs/engine.key";

    const char* node_id_env = std::getenv("FILEGROUP_NODE_ID");
    const char* reg_nodes_env = std::getenv("FILEGROUP_REGISTRY_NODES");
    const char* sto_nodes_env = std::getenv("FILEGROUP_STORAGE_NODES");
    uint16_t node_id = node_id_env ? static_cast<uint16_t>(std::atoi(node_id_env)) : 0;
    const bool cluster_mode = node_id != 0 && reg_nodes_env && sto_nodes_env;

    if (cluster_mode) {
        // Format: "1=node1:50051,2=node2:50051,3=node3:50051"
        for (const auto& entry : split_csv(reg_nodes_env)) {
            auto eq = entry.find('=');
            if (eq == std::string::npos) continue;
            filegroup::RegistryNodeConfig rn;
            rn.id = static_cast<uint16_t>(std::atoi(entry.substr(0, eq).c_str()));
            rn.address = entry.substr(eq + 1);
            rn.cert_file = "certs/ca.crt";
            config.registry_nodes.push_back(rn);
        }
        // Format: "1=node1:5001,2=node2:5001,3=node3:5001"
        for (const auto& entry : split_csv(sto_nodes_env)) {
            auto eq = entry.find('=');
            if (eq == std::string::npos) continue;
            filegroup::StorageNodeConfig sn;
            sn.node_id = static_cast<uint16_t>(std::atoi(entry.substr(0, eq).c_str()));
            sn.address = entry.substr(eq + 1);
            sn.role = filegroup::NodeRole::ORIGIN;
            sn.cert_file = "certs/engine.crt";
            config.storage_nodes.push_back(sn);
        }
    } else {
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
    }

    filegroup::FileGroupConfig group;
    group.group_id = 1;
    group.name = "default";
    group.chunk_size = 65536;
    group.min_chunk_bytes = 1024;
    const char* repl_env = std::getenv("FILEGROUP_REPLICATION");
    group.replication_factor = static_cast<uint8_t>(
        repl_env ? std::atoi(repl_env) : (cluster_mode ? 3 : 1));
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
    const std::string data_root = data_dir ? data_dir : "/var/lib/filegroup";

    std::unique_ptr<filegroup::ClusterNode> cluster;
    std::unique_ptr<filegroup::EngineServer> engine_server;
    filegroup::EngineServer* engine = nullptr;

    if (cluster_mode) {
        std::cout << "Cluster mode: node " << node_id << " of "
                  << config.registry_nodes.size() << " registry nodes\n";
        cluster = std::make_unique<filegroup::ClusterNode>(config, &metrics, data_root, node_id);
        engine = &cluster->server();
    } else {
        engine_server = std::make_unique<filegroup::EngineServer>(config, &metrics, data_root);
        engine = engine_server.get();
    }

    // ── Start gRPC server ────────────────────────────────────────────────
    std::string server_address("0.0.0.0:8443");
    EngineGrpcService service(*engine);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Engine listening on " << server_address << "\n";
    std::cout << "Press Ctrl+C to stop.\n";

    server->Wait();
    return 0;
}
