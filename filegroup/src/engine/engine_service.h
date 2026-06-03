#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "config/config.h"
#include "engine/engine.h"
#include "engine/session.h"
#include "registry/registry_service.h"
#include "storage/storage_node_service.h"
namespace filegroup {
class EngineServer {
public:
    EngineServer(const ClusterConfig& config, const std::string& data_root="/tmp/filegroup");
    struct R { bool success=false; uint64_t sid=0,fid=0,lid=0,vn=0,cs=0; EncryptionAlgo enc=EncryptionAlgo::NONE; std::string err; };
    R open_session(uint32_t gid,uint32_t tid,uint64_t lid,uint64_t ts=0,uint32_t ec=0,uint32_t fed=0);
    struct WR { bool success=false,ac=false; std::string err; };
    WR write_chunk(uint64_t sid,uint32_t ci,const std::vector<uint8_t>& d);
    struct CR { bool success=false; uint64_t lid=0,fid=0,vn=0; std::string err; };
    CR complete_session(uint64_t sid,uint32_t cs=0);
    struct RR { bool success=false; uint64_t sid=0,fid=0,lid=0,vn=0,cs=0; std::vector<uint32_t> cc; EncryptionAlgo enc=EncryptionAlgo::NONE; std::string err; };
    RR resume_session(uint64_t sid);
    struct RFR { std::vector<uint8_t> d; std::string err; };
    RFR read_file(uint64_t lid,uint32_t vn=0);
    struct RCR { std::vector<uint8_t> d; std::string err; };
    RCR read_chunk(uint64_t lid,uint32_t vn,uint32_t ci);
    struct SR { bool success=false; std::string err; };
    SR delete_file(uint64_t lid),delete_version(uint64_t lid,uint32_t vn),cancel_session(uint64_t sid);
    struct FIR { bool success=false; uint64_t lid=0; uint32_t tid=0,gid=0,lv=0; FileState st=FileState::ACTIVE; std::string err; };
    FIR get_file_info(uint64_t lid);
    struct VIR { uint64_t fid=0; uint32_t vn=0; VersionState st=VersionState::UPLOADING; uint64_t ts=0; uint32_t cc=0; uint64_t ea=0; EncryptionAlgo enc=EncryptionAlgo::NONE; };
    std::vector<VIR> list_versions(uint64_t lid);
    std::vector<FIR> list_files(uint32_t gid,uint32_t tid);
    bool ping() const;
    Engine& engine(){return *engine_;}
private:
    ClusterConfig config_; std::unique_ptr<RegistryServer> rs_;
    std::vector<std::unique_ptr<StorageServer>> ss_; std::vector<std::unique_ptr<StorageClient>> sc_;
    std::unique_ptr<RegistryClient> rc_; std::unique_ptr<Engine> engine_;
};
}
