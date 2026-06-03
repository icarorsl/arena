#include "engine/engine_service.h"
#include <algorithm>
namespace filegroup {
EngineServer::EngineServer(const ClusterConfig& c,const std::string& dr):config_(c){
 for(size_t i=0;i<c.storage_nodes.size();i++){auto& nc=c.storage_nodes[i];
  auto s=std::make_unique<StorageServer>(nc.node_id,dr+"/node_"+std::to_string(nc.node_id));
  auto cl=std::make_unique<StorageClient>(s.get());ss_.push_back(std::move(s));sc_.push_back(std::move(cl));}
 rs_=std::make_unique<RegistryServer>(1,std::vector<uint32_t>{1},dr+"/raft.log",0);
 rc_=std::make_unique<RegistryClient>(std::vector<RegistryServer*>{rs_.get()});
 rs_->wait_for_leader(3000000);
 std::vector<StorageClient*> cp;for(auto&x:sc_)cp.push_back(x.get());
 engine_=std::make_unique<Engine>(c,rc_.get(),cp);
}
EngineServer::R EngineServer::open_session(uint32_t gid,uint32_t tid,uint64_t lid,uint64_t ts,uint32_t ec,uint32_t fed){R r;try{auto s=engine_->open_session(gid,tid,lid,ts,ec,fed);r.success=true;r.sid=s.session_id;r.fid=s.file_id;r.lid=s.logical_file_id;r.vn=s.version_number;r.cs=s.resolved_chunk_size;r.enc=s.resolved_encryption;}catch(const std::exception& e){r.err=e.what();}return r;}
EngineServer::WR EngineServer::write_chunk(uint64_t sid,uint32_t ci,const std::vector<uint8_t>& d){WR r;auto* s=engine_->get_session(sid);if(s&&s->confirmed_chunks.count(ci)){r.success=true;r.ac=true;return r;}bool ok=engine_->write_chunk(sid,ci,d.data(),d.size());r.success=ok;if(!ok)r.err="write chunk failed";return r;}
EngineServer::CR EngineServer::complete_session(uint64_t sid,uint32_t cs){CR r;bool ok=engine_->complete_session(sid,cs);r.success=ok;if(ok){auto* s=engine_->get_session(sid);if(s){r.lid=s->logical_file_id;r.fid=s->file_id;r.vn=s->version_number;}}else r.err="complete failed";return r;}
EngineServer::RR EngineServer::resume_session(uint64_t sid){RR r;r.sid=sid;auto* s=engine_->get_session(sid);if(!s){r.err="session not found";return r;}r.success=true;r.fid=s->file_id;r.lid=s->logical_file_id;r.vn=s->version_number;r.cs=s->resolved_chunk_size;r.enc=s->resolved_encryption;auto cc=engine_->resume_session(sid);r.cc=std::move(cc);return r;}
EngineServer::RFR EngineServer::read_file(uint64_t lid,uint32_t vn){RFR r;r.d=engine_->read_file(lid,vn);if(r.d.empty())r.err="not found";return r;}
EngineServer::RCR EngineServer::read_chunk(uint64_t lid,uint32_t vn,uint32_t ci){RCR r;r.d=engine_->read_chunk(lid,vn,ci);if(r.d.empty())r.err="not found";return r;}
EngineServer::SR EngineServer::delete_file(uint64_t lid){FileDeletedEntry e;e.logical_file_id=lid;auto[ok,lsn]=rc_->append_entry((uint32_t)ManifestEntryType::FILE_DELETED,&e,sizeof(e));(void)lsn;return{ok,ok?"":"delete failed"};}
EngineServer::SR EngineServer::delete_version(uint64_t lid,uint32_t vn){return{false,"not implemented"};}
EngineServer::FIR EngineServer::get_file_info(uint64_t lid){FIR r;r.lid=lid;auto* f=rc_->get_file(lid);if(!f){r.err="not found";return r;}r.success=true;r.tid=f->table_id;r.gid=f->group_id;r.lv=f->latest_complete_version;return r;}
std::vector<EngineServer::VIR> EngineServer::list_versions(uint64_t lid){std::vector<VIR> r;auto* f=rc_->get_file(lid);if(!f)return r;for(auto&[vn,ver]:f->versions){VIR v;v.fid=ver.file_id;v.vn=ver.version_number;v.st=ver.state;v.ts=ver.total_size;v.cc=ver.chunk_count;v.ea=ver.expires_at;v.enc=ver.encryption;r.push_back(v);}return r;}
std::vector<EngineServer::FIR> EngineServer::list_files(uint32_t gid,uint32_t tid){std::vector<FIR> r;auto fs=rc_->list_files((uint16_t)tid,gid);for(auto&f:fs){FIR i;i.success=true;i.lid=f.logical_file_id;i.tid=f.table_id;i.gid=f.group_id;i.lv=f.latest_complete_version;r.push_back(i);}return r;}
EngineServer::SR EngineServer::cancel_session(uint64_t sid){auto* s=engine_->get_session(sid);if(!s)return{false,"not found"};return{true,""};}
bool EngineServer::ping()const{return true;}
}
