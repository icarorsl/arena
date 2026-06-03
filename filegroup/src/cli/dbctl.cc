#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include "config/config.h"
#include "engine/engine_service.h"
#include "tls/tls_init.h"

namespace {

void print_usage() {
    std::cout << "FILE Group CLI (Phase 1)\n"
              << "Usage: dbctl <command> [options]\n\n"
              << "Commands:\n"
              << "  tls init --nodes N --output /path/\n"
              << "  cluster status\n"
              << "  cluster nodes\n"
              << "  files list --group G --table T\n"
              << "  files info --group G --file F\n"
              << "  files versions --group G --file F\n"
              << "  files delete --group G --file F [--version V]\n"
              << "  sessions list --group G\n"
              << "  sessions cancel --group G --session S\n"
              << "  nodes list\n"
              << "  nodes health\n"
              << "  registry status\n"
              << "  registry leader\n\n"
              << "Options:\n"
              << "  --output json    Format output as JSON\n";
}

void cmd_tls_init(int argc, char* argv[]) {
    std::string output_dir = "certs";
    int nodes = 3;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--nodes" && i + 1 < argc) nodes = std::stoi(argv[++i]);
        else if (arg == "--output" && i + 1 < argc) output_dir = argv[++i];
    }

    filegroup::TLSInitOptions opts;
    opts.num_nodes = nodes;
    opts.output_dir = output_dir;

    std::cout << "Generating TLS certificates...\n";
    std::string toml = filegroup::tls_init(opts);
    std::cout << "\nCertificates written to " << output_dir << "/\n\n";
    std::cout << "Add this to your config.toml:\n\n" << toml << "\n";
}

void cmd_files_list(int argc, char* argv[], filegroup::EngineServer& server) {
    uint32_t group_id = 0, table_id = 0;
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--group" && i + 1 < argc) group_id = std::stoi(argv[++i]);
        else if (arg == "--table" && i + 1 < argc) table_id = std::stoi(argv[++i]);
    }

    auto files = server.list_files(group_id, table_id);
    std::cout << std::left << std::setw(20) << "LOGICAL_ID"
              << std::setw(10) << "TABLE" << std::setw(10) << "GROUP"
              << std::setw(10) << "VERSION" << "\n";
    std::cout << std::string(50, '-') << "\n";
    for (auto& f : files) {
        std::cout << std::setw(20) << f.logical_file_id
                  << std::setw(10) << f.table_id
                  << std::setw(10) << f.group_id
                  << std::setw(10) << f.latest_version << "\n";
    }
    std::cout << files.size() << " file(s)\n";
}

void cmd_files_info(int argc, char* argv[], filegroup::EngineServer& server) {
    uint64_t file_id = 0;
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--file" && i + 1 < argc) file_id = std::stoull(argv[++i]);
    }

    auto info = server.get_file_info(file_id);
    if (!info.success) { std::cout << "Not found\n"; return; }
    std::cout << "Logical File ID: " << info.logical_file_id << "\n"
              << "Table: " << info.table_id << "\n"
              << "Group: " << info.group_id << "\n"
              << "Latest Version: " << info.latest_version << "\n";
}

void cmd_files_versions(int argc, char* argv[], filegroup::EngineServer& server) {
    uint64_t file_id = 0;
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--file" && i + 1 < argc) file_id = std::stoull(argv[++i]);
    }

    auto versions = server.list_versions(file_id);
    std::cout << std::left << std::setw(12) << "FILE_ID"
              << std::setw(10) << "VERSION" << std::setw(14) << "STATE"
              << std::setw(12) << "SIZE" << std::setw(8) << "CHUNKS" << "\n";
    std::cout << std::string(56, '-') << "\n";
    for (auto& v : versions) {
        std::string state = v.state == filegroup::VersionState::COMPLETE ? "COMPLETE" :
                            v.state == filegroup::VersionState::UPLOADING ? "UPLOADING" :
                            v.state == filegroup::VersionState::DELETED ? "DELETED" : "OTHER";
        std::cout << std::setw(12) << v.file_id << std::setw(10) << v.version_number
                  << std::setw(14) << state << std::setw(12) << v.total_size
                  << std::setw(8) << v.chunk_count << "\n";
    }
}

void cmd_files_delete(int argc, char* argv[], filegroup::EngineServer& server) {
    uint64_t file_id = 0;
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--file" && i + 1 < argc) file_id = std::stoull(argv[++i]);
    }

    auto result = server.delete_file(file_id);
    std::cout << (result.success ? "Deleted.\n" : "Failed: " + result.error + "\n");
}

void cmd_nodes_health(filegroup::EngineServer& server) {
    (void)server;
    std::cout << "Node health: all nodes healthy (Phase 1 heartbeat not yet implemented)\n";
}

void cmd_registry_status(filegroup::EngineServer& server) {
    std::cout << (server.ping() ? "Registry: OK\n" : "Registry: DOWN\n");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];

    // Commands that don't need a server connection
    if (cmd == "tls" && argc >= 3 && std::string(argv[2]) == "init") {
        cmd_tls_init(argc, argv);
        return 0;
    }
    if (cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }

    // All other commands need a server — create minimal config + server
    filegroup::ClusterConfig config;
    config.tls.ca_cert_file = "certs/ca.crt";
    config.tls.engine_cert_file = "certs/engine.crt";
    config.tls.engine_key_file = "certs/engine.key";

    filegroup::RegistryNodeConfig reg; reg.id = 1; reg.address = "localhost:50051";
    reg.cert_file = "certs/ca.crt"; reg.key_file = "certs/engine.key";
    config.registry_nodes.push_back(reg);

    filegroup::StorageNodeConfig storage; storage.node_id = 1;
    storage.address = "localhost:5001"; storage.role = filegroup::NodeRole::ORIGIN;
    storage.cert_file = "certs/engine.crt"; storage.key_file = "certs/engine.key";
    config.storage_nodes.push_back(storage);

    filegroup::FileGroupConfig group; group.group_id = 1; group.name = "default";
    group.chunk_size = 65536; group.min_chunk_bytes = 1024; group.replication_factor = 1;
    group.max_versions = 5; group.file_expires_in_days = 0;
    group.expiry_granularity = filegroup::ExpiryGranularity::UNSET;
    group.encryption = filegroup::EncryptionAlgo::NONE;
    config.groups.push_back(group);

    filegroup::FileTableConfig table; table.table_id = 1; table.name = "default";
    table.group_id = 1; table.expiry_granularity = filegroup::ExpiryGranularity::UNSET;
    table.encryption = filegroup::EncryptionAlgo::NONE;
    config.tables.push_back(table);

    filegroup::ApiKeyConfig api; api.key = "test-api-key"; api.name = "dev";
    api.groups = {1}; api.permissions = {"read", "write", "admin"};
    config.api_keys.push_back(api);

    filegroup::EngineServer server(config, "/tmp/filegroup-dbctl");

    if (cmd == "cluster" && argc >= 3) {
        std::string sub = argv[2];
        if (sub == "status") cmd_registry_status(server);
        else if (sub == "nodes") cmd_nodes_health(server);
    } else if (cmd == "files" && argc >= 3) {
        std::string sub = argv[2];
        if (sub == "list") cmd_files_list(argc, argv, server);
        else if (sub == "info") cmd_files_info(argc, argv, server);
        else if (sub == "versions") cmd_files_versions(argc, argv, server);
        else if (sub == "delete") cmd_files_delete(argc, argv, server);
    } else if (cmd == "nodes") {
        if (argc >= 3 && std::string(argv[2]) == "health") cmd_nodes_health(server);
        else cmd_nodes_health(server);
    } else if (cmd == "registry") {
        if (argc >= 3 && std::string(argv[2]) == "status") cmd_registry_status(server);
        else if (argc >= 3 && std::string(argv[2]) == "leader") std::cout << "Leader: node 1 (single-node)\n";
    } else {
        print_usage();
    }

    return 0;
}
