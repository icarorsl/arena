#include "engine/engine.h"
#include "common/clock.h"
#include "common/crc32c.h"
#include <algorithm>
#include <iostream>
namespace filegroup {
Engine::Engine(const ClusterConfig& c, RegistryClient* r, const std::vector<StorageClient*>& sn):config_(c),registry_(r),storage_nodes_(sn){uint16_t n=1;for(auto* s:sn)if(s)node_map_[n++]=s;}
UploadSession Engine::open_session(uint32_t gid,uint32_t tid,uint64_t lid,uint64_t ts,uint32_t ec,uint32_t fed){
 auto* grp=find_group(gid); if(!grp)throw std::runtime_error("group not found");
 auto* tbl=find_table(tid);
 // Fall back to dynamic tables from registry if not in static config
 FileTableConfig dyn_tbl;
 if(!tbl){
  auto dts=registry_->get_tables();
  for(auto&dt:dts){
   if(dt.table_id==tid&&dt.group_id==gid){
    dyn_tbl.table_id=dt.table_id;dyn_tbl.group_id=dt.group_id;dyn_tbl.name=dt.name;
    dyn_tbl.chunk_size=dt.chunk_size;dyn_tbl.replication_factor=dt.replication_factor;
    dyn_tbl.file_expires_in_days=dt.file_expires_in_days;dyn_tbl.max_versions=dt.max_versions;
    dyn_tbl.encryption=dt.encryption;dyn_tbl.expiry_granularity=dt.expiry_granularity;
    tbl=&dyn_tbl;break;
   }
  }
 }
 if(!tbl) throw std::runtime_error("table "+std::to_string(tid)+" not found in group "+std::to_string(gid));
 auto cs=resolve_chunk_size(*grp,tbl,0); auto rf=resolve_replication_factor(*grp,tbl,0);
 auto ea=resolve_expires_at(*grp,tbl,fed,now_us()); auto enc=resolve_encryption(*grp,tbl);
 auto mv=resolve_max_versions(*grp,tbl);
 // If adding a new version to an existing file, check max_versions cap
 if(lid && mv>0){
  auto* ef=registry_->get_file(lid);
  if(ef){
   uint32_t complete_count=0;
   for(auto&[vn,ver]:ef->versions) if(ver.state==VersionState::COMPLETE) complete_count++;
   if(complete_count>=mv) throw std::runtime_error("max_versions="+std::to_string(mv)+" reached ("+std::to_string(complete_count)+" live versions); delete a version first");
  }
 }
 auto st=(ea>0)?SegmentType::PAGE:SegmentType::STANDARD;
 uint64_t sid=registry_->next_session_id(),fid=registry_->next_file_id(),lid2=lid?lid:registry_->next_logical_file_id(); if(!lid)lid=lid2;
 uint32_t vn=1;
 if(lid){
  auto* ef=registry_->get_file(lid);
  if(ef) vn=ef->next_version_number;
 }
 std::vector<uint16_t> hn; for(uint16_t i=1;i<=storage_nodes_.size();i++)hn.push_back(i);
 uint32_t cc=ec; if(cc==0&&ts>0)cc=(uint32_t)((ts+cs-1)/cs);
 auto as=assign_chunks(fid,std::max(1u,cc),rf,hn);
 UploadSession s; s.session_id=sid; s.file_id=fid; s.logical_file_id=lid;
 s.table_id=(uint16_t)tid; s.group_id=gid; s.version_number=vn;
 s.state=VersionState::UPLOADING; s.created_at_us=now_us(); s.last_activity_us=s.created_at_us;
 s.expected_chunks=cc; s.resolved_chunk_size=cs; s.resolved_replication=rf;
 s.resolved_expires_at=ea; s.resolved_encryption=enc; s.segment_type=st;
 s.resolved_max_versions=mv;
 s.chunk_assignments=std::move(as);
 SessionOpenEntry e; e.session_id=sid;e.file_id=fid;e.logical_file_id=lid;
 e.table_id=(uint16_t)tid;e.group_id=gid;e.version_number=vn;
 e.chunk_size=cs;e.replication_factor=rf;e.expected_chunks=cc;e.expires_at=ea;
 e.encryption=(uint8_t)enc;e.segment_type=(uint8_t)st;
 registry_->append_entry((uint32_t)ManifestEntryType::SESSION_OPEN,&e,sizeof(e));
 {std::lock_guard<std::mutex> lk(sessions_mutex_);sessions_[sid]=s;}
 return s;
}
bool Engine::write_chunk(uint64_t sid,uint32_t ci,const uint8_t* d,uint64_t sz){
 UploadSession* s=nullptr;{std::lock_guard<std::mutex>lk(sessions_mutex_);auto it=sessions_.find(sid);if(it==sessions_.end())return false;s=&it->second;}
 if(s->state!=VersionState::UPLOADING)return false; if(s->confirmed_chunks.count(ci))return true;
 s->last_activity_us=now_us(); uint32_t csum=crc32c(d,sz);
 const ChunkAssignment* a=nullptr;for(auto&x:s->chunk_assignments)if(x.chunk_index==ci){a=&x;break;} if(!a)return false;
 auto* p=get_storage_node(a->primary_node_id);if(!p)return false;
 auto r=p->store_chunk(s->file_id,ci,s->group_id,s->table_id,d,sz,csum,false,s->resolved_expires_at,s->resolved_expires_at>0?ExpiryGranularity::DAY:ExpiryGranularity::UNSET);
 if(!r.success)return false;
 s->total_bytes+=sz;
 for(auto rid:a->replica_node_ids){auto*rep=get_storage_node(rid);if(rep)rep->store_chunk(s->file_id,ci,s->group_id,s->table_id,d,sz,csum,false,s->resolved_expires_at,s->resolved_expires_at>0?ExpiryGranularity::DAY:ExpiryGranularity::UNSET);}
 ChunkConfirmedEntry ce;ce.session_id=sid;ce.file_id=s->file_id;ce.chunk_index=ci;ce.chunk_size_actual=sz;ce.chunk_checksum=csum;ce.replica_count=s->resolved_replication;
 registry_->append_entry((uint32_t)ManifestEntryType::CHUNK_CONFIRMED,&ce,sizeof(ce));
 {std::lock_guard<std::mutex> lk(sessions_mutex_);s->confirmed_chunks.insert(ci);}
 {std::lock_guard<std::mutex> lk(chunks_mutex_);chunk_locs_[s->file_id][ci]={r.segment_file,r.offset,sz};}
 if(s->expected_chunks>0&&s->confirmed_chunks.size()>=s->expected_chunks)complete_session(sid,0);
 return true;
}
bool Engine::complete_session(uint64_t sid,uint32_t cs,std::string* note){
 UploadSession* s=nullptr;{std::lock_guard<std::mutex>lk(sessions_mutex_);auto it=sessions_.find(sid);if(it==sessions_.end())return false;s=&it->second;}
 if(s->state==VersionState::COMPLETE)return true; if(s->state!=VersionState::UPLOADING)return false;
 if(s->expected_chunks>0&&s->confirmed_chunks.size()<s->expected_chunks)return false;
 VersionCompleteEntry e;e.file_id=s->file_id;e.logical_file_id=s->logical_file_id;e.version_number=s->version_number;e.content_checksum=cs;e.total_size=s->total_bytes;e.chunk_count=(uint32_t)s->confirmed_chunks.size();e.created_at_us=s->created_at_us;
 registry_->append_entry((uint32_t)ManifestEntryType::VERSION_COMPLETE,&e,sizeof(e));
 {std::lock_guard<std::mutex> lk(sessions_mutex_);s->state=VersionState::COMPLETE;}
 // Enforce max_versions
 if(s->resolved_max_versions>0){
  auto* f=registry_->get_file(s->logical_file_id);
  if(f){
   std::vector<uint32_t> complete_versions;
   for(auto&[vn,ver]:f->versions) if(ver.state==VersionState::COMPLETE) complete_versions.push_back(vn);
   std::sort(complete_versions.begin(),complete_versions.end());
   while(complete_versions.size()>s->resolved_max_versions){
    uint32_t oldest_vn=complete_versions.front();
    VersionDeletedEntry vd;vd.file_id=f->versions.at(oldest_vn).file_id;vd.logical_file_id=s->logical_file_id;vd.version_number=oldest_vn;
    registry_->append_entry((uint32_t)ManifestEntryType::VERSION_DELETED,&vd,sizeof(vd));
    MaxVersionsEnforcedEntry mv;mv.logical_file_id=s->logical_file_id;mv.deleted_version_number=oldest_vn;mv.deleted_file_id=f->versions.at(oldest_vn).file_id;
    registry_->append_entry((uint32_t)ManifestEntryType::MAX_VERSIONS_ENFORCED,&mv,sizeof(mv));
    std::cout << "[engine] max_versions: deleted v" << oldest_vn << " of file " << s->logical_file_id << "\n";
    if(note) *note = "Auto-deleted v" + std::to_string(oldest_vn) + " (max_versions=" + std::to_string(s->resolved_max_versions) + ")";
    complete_versions.erase(complete_versions.begin());
   }
  }
 }
 return true;
}
std::vector<uint32_t> Engine::resume_session(uint64_t sid){
 UploadSession* s=nullptr;{std::lock_guard<std::mutex>lk(sessions_mutex_);auto it=sessions_.find(sid);if(it==sessions_.end())return{};s=&it->second;}
 return std::vector<uint32_t>(s->confirmed_chunks.begin(),s->confirmed_chunks.end());
}
std::vector<uint8_t> Engine::read_file(uint64_t lid,uint32_t v){
 const VersionEntry* ve=v>0?registry_->get_version(lid,v):registry_->get_latest_complete(lid);
 if(!ve||(ve->state!=VersionState::COMPLETE&&ve->state!=VersionState::SUPERSEDED))return{};
 std::vector<uint8_t> r;
 for(uint32_t ci=0;ci<ve->chunk_count;ci++){ChunkLoc l;{std::lock_guard<std::mutex>lk(chunks_mutex_);auto fit=chunk_locs_.find(ve->file_id);if(fit==chunk_locs_.end())return{};auto cit=fit->second.find(ci);if(cit==fit->second.end())return{};l=cit->second;}
  bool ok=false;for(auto* n:storage_nodes_){auto f=n->fetch_chunk(l.sf,l.off,l.sz);if(f.success){r.insert(r.end(),f.data.begin(),f.data.end());ok=true;break;}}if(!ok)return{};}
 if(ve->content_checksum!=0&&crc32c(r.data(),r.size())!=ve->content_checksum)return{};
 return r;
}
std::vector<uint8_t> Engine::read_chunk(uint64_t lid,uint32_t v,uint32_t ci){auto d=read_file(lid,v);return d;}
const UploadSession* Engine::get_session(uint64_t sid)const{std::lock_guard<std::mutex>lk(sessions_mutex_);auto it=sessions_.find(sid);return it!=sessions_.end()?&it->second:nullptr;}
void Engine::update_chunk_location(uint64_t fid,uint32_t ci,const std::string& sf,uint64_t off,uint64_t sz){
 std::lock_guard<std::mutex> lk(chunks_mutex_);
 chunk_locs_[fid][ci]={sf,off,sz};
}
const FileGroupConfig* Engine::find_group(uint32_t gid)const{for(auto&g:config_.groups)if(g.group_id==gid)return&g;return nullptr;}
const FileTableConfig* Engine::find_table(uint32_t tid)const{for(auto&t:config_.tables)if(t.table_id==tid)return&t;return nullptr;}
StorageClient* Engine::get_storage_node(uint16_t nid){auto it=node_map_.find(nid);return it!=node_map_.end()?it->second:nullptr;}
}
