#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "config/config.h"
#include "engine/engine.h"
#include "engine/session.h"
#include "metrics/metrics.h"
#include "registry/registry_service.h"
#include "storage/storage_node_service.h"
namespace filegroup {
class EngineServer {
public:
    EngineServer(const ClusterConfig& config, MetricsServer* metrics=nullptr, const std::string& data_root="/tmp/filegroup");
    struct R { bool success=false; uint64_t session_id=0,file_id=0,logical_file_id=0,version_number=0,resolved_chunk_size=0; EncryptionAlgo encryption=EncryptionAlgo::NONE; std::string error; };
    R open_session(uint32_t gid,uint32_t tid,uint64_t lid,uint64_t ts=0,uint32_t ec=0,uint32_t fed=0);
    struct WR { bool success=false,already_confirmed=false; std::string error; };
    WR write_chunk(uint64_t sid,uint32_t ci,const std::vector<uint8_t>& d);
    struct CR { bool success=false; uint64_t logical_file_id=0,file_id=0,version_number=0; std::string error; };
    CR complete_session(uint64_t sid,uint32_t cs=0);
    struct RR { bool success=false; uint64_t session_id=0,file_id=0,logical_file_id=0,version_number=0,resolved_chunk_size=0; std::vector<uint32_t> confirmed_chunks; EncryptionAlgo encryption=EncryptionAlgo::NONE; std::string error; };
    RR resume_session(uint64_t sid);
    struct RFR { std::vector<uint8_t> data; std::string error; };
    RFR read_file(uint64_t lid,uint32_t vn=0);
    struct RCR { std::vector<uint8_t> data; std::string error; };
    RCR read_chunk(uint64_t lid,uint32_t vn,uint32_t ci);
    struct SR { bool success=false; std::string error; };
    SR delete_file(uint64_t lid),delete_version(uint64_t lid,uint32_t vn),cancel_session(uint64_t sid);
    struct FIR { bool success=false; uint64_t logical_file_id=0,total_size=0,created_at_us=0; uint32_t table_id=0,group_id=0,latest_version=0; FileState state=FileState::ACTIVE; std::string error; };
    FIR get_file_info(uint64_t lid);
    struct VIR { uint64_t file_id=0; uint32_t version_number=0; VersionState state=VersionState::UPLOADING; uint64_t total_size=0,expires_at_us=0,created_at_us=0; uint32_t chunk_count=0; EncryptionAlgo encryption=EncryptionAlgo::NONE; };
    struct TIR { uint32_t table_id=0,group_id=0; std::string name; uint64_t chunk_size=0; uint8_t replication_factor=0; EncryptionAlgo encryption=EncryptionAlgo::NONE; uint32_t max_versions=0,file_expires_in_days=0; ExpiryGranularity expiry_granularity=ExpiryGranularity::UNSET; };
    std::vector<VIR> list_versions(uint64_t lid);
    std::vector<FIR> list_files(uint32_t gid,uint32_t tid);
    std::vector<TIR> get_tables();
    SR create_table(uint32_t tid,uint32_t gid,const std::string& name,uint32_t fed=0,uint32_t mv=0);
    bool ping() const;
    Engine& engine(){return *engine_;}
private:
    ClusterConfig config_; MetricsServer* metrics_=nullptr;
    std::unique_ptr<RegistryServer> rs_;
    std::vector<std::unique_ptr<StorageServer>> ss_; std::vector<std::unique_ptr<StorageClient>> sc_;
    std::unique_ptr<RegistryClient> rc_; std::unique_ptr<Engine> engine_;
};
}
