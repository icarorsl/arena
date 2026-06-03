#pragma once

#include <cstdint>
#include <string>

namespace filegroup {

// File and version state enums
enum class FileState : uint8_t {
    ACTIVE = 0x00,
    DELETED = 0x01,
};

enum class VersionState : uint8_t {
    UPLOADING = 0x00,
    COMPLETE = 0x01,
    SUPERSEDED = 0x02,
    DELETED = 0x03,
    EXPIRED = 0x04,
    SESSION_TIMED_OUT = 0x05,
};

// Encryption algorithm
enum class EncryptionAlgo : uint8_t {
    NONE = 0x00,
    AES_256_GCM = 0x01,
};

// Page expiry granularity
enum class ExpiryGranularity : uint8_t {
    DAY = 0x00,
    WEEK = 0x01,
    MONTH = 0x02,
    UNSET = 0xFF,
};

// Node role (Phase 2 concept, only ORIGIN in Phase 1)
enum class NodeRole : uint8_t {
    ORIGIN = 0x00,
    EDGE = 0x01,  // Phase 2 — not implemented
};

// Replica state in storage
enum class ReplicaState : uint8_t {
    PENDING = 0x00,
    WRITTEN = 0x01,
    VERIFIED = 0x02,
    DELETE_PENDING = 0x03,
    CORRUPT = 0x04,
};

// Segment type
enum class SegmentType : uint8_t {
    STANDARD = 0x00,
    PAGE = 0x01,
};

// Node health state
enum class NodeState : uint8_t {
    HEALTHY = 0x00,
    SUSPECT = 0x01,
    DEAD = 0x02,
};

// Manifest entry types
enum class ManifestEntryType : uint16_t {
    SESSION_OPEN = 0x0000,
    CHUNK_CONFIRMED = 0x0001,
    VERSION_COMPLETE = 0x0002,
    VERSION_DELETED = 0x0003,
    FILE_DELETED = 0x0004,
    SESSION_TIMED_OUT = 0x0005,
    CHUNK_DELETE_CONFIRMED = 0x0006,
    PAGE_EXPIRED = 0x0007,
    PAGE_DELETED = 0x0008,
    NODE_HEALTH = 0x0009,
    MAX_VERSIONS_ENFORCED = 0x000A,
    CHUNK_LOCATION_UPDATED = 0x000B,
    TABLE_CREATED = 0x0010,
    // Phase 3 projection entries (declared for forward compatibility)
    PROJECTION_CREATE = 0x000C,    // Phase 3 — not implemented
    PROJECTION_UPDATE = 0x000D,    // Phase 3 — not implemented
    PROJECTION_DELETE = 0x000E,    // Phase 3 — not implemented
    PROJECTION_STATE = 0x000F,     // Phase 3 — not implemented
};

// Helper functions
inline std::string version_state_to_string(VersionState state) {
    switch (state) {
        case VersionState::UPLOADING:        return "UPLOADING";
        case VersionState::COMPLETE:         return "COMPLETE";
        case VersionState::SUPERSEDED:       return "SUPERSEDED";
        case VersionState::DELETED:          return "DELETED";
        case VersionState::EXPIRED:          return "EXPIRED";
        case VersionState::SESSION_TIMED_OUT: return "SESSION_TIMED_OUT";
        default:                             return "UNKNOWN";
    }
}

inline std::string node_state_to_string(NodeState state) {
    switch (state) {
        case NodeState::HEALTHY: return "HEALTHY";
        case NodeState::SUSPECT: return "SUSPECT";
        case NodeState::DEAD:    return "DEAD";
        default:                 return "UNKNOWN";
    }
}

}  // namespace filegroup
