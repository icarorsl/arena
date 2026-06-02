#include "read/read_protocol.h"

namespace filegroup {

ReadProtocol::ReadProtocol() {}
ReadSessionInfo ReadProtocol::open_session(const ReadSessionConfig&) { return {}; }
bool ReadProtocol::has_session(uint32_t) const { return false; }
ReadSessionInfo ReadProtocol::get_session(uint32_t) const { return {}; }
ChunkReadResponse ReadProtocol::read_chunk(const ChunkReadRequest&) { return {}; }
bool ReadProtocol::complete_session(uint32_t) { return false; }
bool ReadProtocol::cancel_session(uint32_t) { return false; }

}  // namespace filegroup
