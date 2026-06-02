#include "file_header/file_header.h"

#include <cstring>
#include <stdexcept>
#include <sstream>

namespace filegroup {

void serialize_file_header(const FileHeader& header, uint8_t* buf) {
    // Write directly using memcpy (struct is already packed)
    std::memcpy(buf, &header, sizeof(FileHeader));
}

FileHeader deserialize_file_header(const uint8_t* buf) {
    FileHeader header;
    std::memcpy(&header, buf, sizeof(FileHeader));
    
    // Check version immediately — must be valid before using any other fields
    if (header.file_header_version != FILE_HEADER_VERSION) {
        std::stringstream ss;
        ss << "Unsupported file header version: 0x" << std::hex << (int)header.file_header_version;
        throw std::runtime_error(ss.str());
    }
    
    return header;
}

bool validate_file_header(const FileHeader& header) {
    // Check version
    if (header.file_header_version != FILE_HEADER_VERSION) {
        return false;
    }
    
    // Check reserved bytes are zero
    if (header.reserved[0] != 0 || header.reserved[1] != 0 || header.reserved[2] != 0) {
        return false;
    }
    
    // Check chunk_size is positive
    if (header.chunk_size == 0) {
        return false;
    }
    
    // Check replication_factor is reasonable (1-255)
    if (header.replication_factor == 0) {
        return false;
    }
    
    return true;
}

}  // namespace filegroup
