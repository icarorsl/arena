#include "engine/engine_service.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <sys/stat.h>
#include "common/clock.h"
namespace filegroup {
EngineServer::EngineServer(const ClusterConfig& c,MetricsServer* m,const std::string& dr):config_(c),metrics_(m){
 mkdir(dr.c_str(), 0755); // ensure parent dir exists
 for(size_t i=0;i<c.storage_nodes.size();i++){auto& nc=c.storage_nodes[i];
  auto s=std::make_unique<StorageServer>(nc.node_id,dr+"/node_"+std::to_string(nc.node_id));
  auto cl=std::make_unique<StorageClient>(s.get());ss_.push_back(std::move(s));sc_.push_back(std::move(cl));}
 rs_=std::make_unique<RegistryServer>(1,std::vector<uint32_t>{1},dr+"/raft.log",0);
 rc_=std::make_unique<RegistryClient>(std::vector<RegistryServer*>{rs_.get()});
 rs_->wait_for_leader(3000000);
 std::vector<StorageClient*> cp;for(auto&x:sc_)cp.push_back(x.get());
 engine_=std::make_unique<Engine>(c,rc_.get(),cp);
 engine_->rebuild_chunk_locations();
 heartbeat_=std::make_unique<HeartbeatService>(cp,rc_.get(),5);
 heartbeat_->start();
 expiry_=std::make_unique<ExpiryService>(rc_.get(),60);
 expiry_->start();
 compaction_=std::make_unique<CompactionService>(*engine_,rc_.get(),cp,60);
 compaction_->start();
}
EngineServer::R EngineServer::open_session(uint32_t gid,uint32_t tid,uint64_t lid,uint64_t ts,uint32_t ec,uint32_t fed){
 R r;try{auto s=engine_->open_session(gid,tid,lid,ts,ec,fed);
  r.success=true;r.session_id=s.session_id;r.file_id=s.file_id;r.logical_file_id=s.logical_file_id;
  r.version_number=s.version_number;r.resolved_chunk_size=s.resolved_chunk_size;r.encryption=s.resolved_encryption;
  if(metrics_) metrics_->inc_counter("file_upload_sessions_total");
 }catch(const std::exception& e){r.error=e.what();}return r;
}
EngineServer::WR EngineServer::write_chunk(uint64_t sid,uint32_t ci,const std::vector<uint8_t>& d){
 WR r;auto* s=engine_->get_session(sid);
 if(s&&s->confirmed_chunks.count(ci)){r.success=true;r.already_confirmed=true;return r;}
 auto t0=now_us();
 bool ok=engine_->write_chunk(sid,ci,d.data(),d.size());r.success=ok;if(!ok)r.error="write failed";
 if(metrics_){if(ok)metrics_->inc_chunks_confirmed(); metrics_->observe_chunk_write_latency_ms((now_us()-t0)/1000.0);}
 return r;
}
EngineServer::CR EngineServer::complete_session(uint64_t sid,uint32_t cs){
 CR r;std::string note;bool ok=engine_->complete_session(sid,cs,&note);r.success=ok;
 if(ok){auto* s=engine_->get_session(sid);if(s){r.logical_file_id=s->logical_file_id;r.file_id=s->file_id;r.version_number=s->version_number;}
  if(!note.empty())r.error=note;
  if(metrics_) metrics_->inc_counter("file_upload_completions_total");}
 else r.error="complete failed";return r;
}
EngineServer::RR EngineServer::resume_session(uint64_t sid){
 RR r;r.session_id=sid;auto* s=engine_->get_session(sid);
 if(!s){r.error="not found";return r;}
 r.success=true;r.file_id=s->file_id;r.logical_file_id=s->logical_file_id;r.version_number=s->version_number;
 r.resolved_chunk_size=s->resolved_chunk_size;r.encryption=s->resolved_encryption;
 r.confirmed_chunks=engine_->resume_session(sid);return r;
}
EngineServer::RFR EngineServer::read_file(uint64_t lid,uint32_t vn){RFR r;
 auto t0=now_us();r.data=engine_->read_file(lid,vn);
 if(r.data.empty())r.error="not found";
 else if(metrics_){metrics_->inc_counter("file_read_total");metrics_->observe_chunk_read_latency_ms((now_us()-t0)/1000.0);}
 return r;}
EngineServer::RCR EngineServer::read_chunk(uint64_t lid,uint32_t vn,uint32_t ci){RCR r;r.data=engine_->read_chunk(lid,vn,ci);if(r.data.empty())r.error="not found";return r;}
EngineServer::SR EngineServer::delete_file(uint64_t lid){
 auto* f=rc_->get_file(lid);
 if(!f)return{false,"file not found"};
 // Collect version numbers before writing (avoid iterator invalidation)
 std::vector<uint32_t> vns;for(auto&[vn,_]:f->versions)vns.push_back(vn);
 for(auto vn:vns){
  auto vit=f->versions.find(vn);
  if(vit==f->versions.end())continue;
  VersionDeletedEntry e;e.file_id=vit->second.file_id;e.logical_file_id=lid;e.version_number=vn;
  auto[ok,lsn]=rc_->append_entry((uint32_t)ManifestEntryType::VERSION_DELETED,&e,sizeof(e));(void)lsn;
  if(!ok)return{false,"delete failed at version "+std::to_string(vn)};
 }
 return{true,""};
}
EngineServer::SR EngineServer::delete_version(uint64_t lid,uint32_t vn){
 auto* f=rc_->get_file(lid);
 if(!f)return{false,"file not found"};
 auto vit=f->versions.find(vn);
 if(vit==f->versions.end())return{false,"version not found"};
 VersionDeletedEntry e;e.file_id=vit->second.file_id;e.logical_file_id=lid;e.version_number=vn;
 auto[ok,lsn]=rc_->append_entry((uint32_t)ManifestEntryType::VERSION_DELETED,&e,sizeof(e));(void)lsn;
 return{ok,ok?"":"delete failed"};
}
EngineServer::FIR EngineServer::get_file_info(uint64_t lid){
 FIR r;r.logical_file_id=lid;auto* f=rc_->get_file(lid);
 if(!f){r.error="not found";return r;}
 r.success=true;r.table_id=f->table_id;r.group_id=f->group_id;r.latest_version=f->latest_complete_version;
 auto vit=f->versions.find(f->latest_complete_version);
 if(vit!=f->versions.end()){r.total_size=vit->second.total_size;r.created_at_us=vit->second.created_at_us;r.state=(vit->second.state==VersionState::DELETED||vit->second.state==VersionState::MARKED_DELETED)?FileState::DELETED:FileState::ACTIVE;}
 return r;
}
std::vector<EngineServer::VIR> EngineServer::list_versions(uint64_t lid){
 std::vector<VIR> r;auto* f=rc_->get_file(lid);if(!f)return r;
 for(auto&[vn,ver]:f->versions){VIR v;v.file_id=ver.file_id;v.version_number=ver.version_number;
  v.state=ver.state;v.total_size=ver.total_size;v.chunk_count=ver.chunk_count;
  v.expires_at_us=ver.expires_at;v.encryption=ver.encryption;v.created_at_us=ver.created_at_us;r.push_back(v);}
 return r;
}
std::vector<EngineServer::FIR> EngineServer::list_files(uint32_t gid,uint32_t tid){
 std::vector<FIR> r;auto fs=rc_->list_files((uint16_t)tid,gid);
 for(auto&f:fs){
  FIR i;i.success=true;i.logical_file_id=f.logical_file_id;i.table_id=f.table_id;i.group_id=f.group_id;i.latest_version=f.latest_complete_version;
  auto vit=f.versions.find(f.latest_complete_version);
  if(vit!=f.versions.end()){i.total_size=vit->second.total_size;i.state=(vit->second.state==VersionState::DELETED||vit->second.state==VersionState::MARKED_DELETED)?FileState::DELETED:FileState::ACTIVE;i.created_at_us=vit->second.created_at_us;}
  r.push_back(i);}
 return r;
}
EngineServer::SR EngineServer::cancel_session(uint64_t sid){auto* s=engine_->get_session(sid);if(!s)return{false,"not found"};return{true,""};}
bool EngineServer::ping()const{return true;}

std::vector<EngineServer::TIR> EngineServer::get_tables(){
 std::vector<TIR> r;std::set<uint32_t> seen;
 // Dynamic tables (from Raft-replicated TABLE_CREATED entries)
 auto ts=rc_->get_tables();
 for(auto&t:ts){TIR i;i.table_id=t.table_id;i.group_id=t.group_id;i.name=t.name;i.chunk_size=t.chunk_size;i.replication_factor=t.replication_factor;i.encryption=t.encryption;i.max_versions=t.max_versions;i.file_expires_in_days=t.file_expires_in_days;i.expiry_granularity=t.expiry_granularity;r.push_back(i);seen.insert(t.table_id);}
 // Static config tables (fallback for tables not yet created dynamically)
 for(auto&t:config_.tables){if(seen.count(t.table_id))continue;TIR i;i.table_id=t.table_id;i.group_id=t.group_id;i.name=t.name;i.chunk_size=t.chunk_size;i.replication_factor=t.replication_factor;i.encryption=t.encryption;i.max_versions=t.max_versions;i.file_expires_in_days=t.file_expires_in_days;i.expiry_granularity=t.expiry_granularity;r.push_back(i);}
 return r;
}

EngineServer::SR EngineServer::create_table(uint32_t tid,uint32_t gid,const std::string& name,uint32_t fed,uint32_t mv){
 TableCreatedEntry e{};e.table_id=tid;e.group_id=gid;strncpy(e.name,name.c_str(),sizeof(e.name)-1);
 e.file_expires_in_days=fed;e.max_versions=mv;
 auto[ok,lsn]=rc_->append_entry((uint32_t)ManifestEntryType::TABLE_CREATED,&e,sizeof(e));(void)lsn;
 return{ok,ok?"":"create failed"};
}
}
