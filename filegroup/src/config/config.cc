#include "config/config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

// Simple TOML parser — for production use cpptoml library if available
// This is a minimal implementation sufficient for our config format

namespace filegroup {

// Helper: check if file exists and is readable
static bool file_exists(const std::string& path) {
    struct stat buffer;
    return stat(path.c_str(), &buffer) == 0;
}

// Helper: trim whitespace from string
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Helper: split string by delimiter
static std::vector<std::string> split(const std::string& str, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(trim(item));
    }
    return result;
}

// Helper: parse quoted string
static std::string parse_string(const std::string& value) {
    std::string v = trim(value);
    if ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')) {
        return v.substr(1, v.length() - 2);
    }
    return v;
}

// Helper: parse integer
static uint64_t parse_uint64(const std::string& value) {
    return std::stoull(trim(value));
}

static uint32_t parse_uint32(const std::string& value) {
    return std::stoul(trim(value));
}

static uint16_t parse_uint16(const std::string& value) {
    return std::stoul(trim(value));
}

static uint8_t parse_uint8(const std::string& value) {
    return std::stoul(trim(value));
}

// Helper: parse float
static float parse_float(const std::string& value) {
    return std::stof(trim(value));
}

// Helper: parse array of strings like ["a", "b", "c"]
static std::vector<std::string> parse_string_array(const std::string& value) {
    std::string v = trim(value);
    if (v.front() != '[' || v.back() != ']') {
        throw std::runtime_error("Invalid array format: " + value);
    }
    v = v.substr(1, v.length() - 2);
    auto parts = split(v, ',');
    std::vector<std::string> result;
    for (const auto& p : parts) {
        result.push_back(parse_string(p));
    }
    return result;
}

// Helper: parse array of integers like [1, 2, 3]
static std::vector<uint32_t> parse_uint32_array(const std::string& value) {
    std::string v = trim(value);
    if (v.front() != '[' || v.back() != ']') {
        throw std::runtime_error("Invalid array format: " + value);
    }
    v = v.substr(1, v.length() - 2);
    auto parts = split(v, ',');
    std::vector<uint32_t> result;
    for (const auto& p : parts) {
        result.push_back(parse_uint32(p));
    }
    return result;
}

// Helper: parse EncryptionAlgo from string
static EncryptionAlgo parse_encryption(const std::string& value) {
    std::string v = trim(value);
    if (v == "\"NONE\"" || v == "'NONE'" || v == "NONE") return EncryptionAlgo::NONE;
    if (v == "\"AES_256_GCM\"" || v == "'AES_256_GCM'" || v == "AES_256_GCM") return EncryptionAlgo::AES_256_GCM;
    throw std::runtime_error("Unknown encryption algorithm: " + value);
}

// Helper: parse ExpiryGranularity from string
static ExpiryGranularity parse_granularity(const std::string& value) {
    std::string v = trim(value);
    if (v == "\"DAY\"" || v == "'DAY'" || v == "DAY") return ExpiryGranularity::DAY;
    if (v == "\"WEEK\"" || v == "'WEEK'" || v == "WEEK") return ExpiryGranularity::WEEK;
    if (v == "\"MONTH\"" || v == "'MONTH'" || v == "MONTH") return ExpiryGranularity::MONTH;
    throw std::runtime_error("Unknown expiry granularity: " + value);
}

// Helper: parse NodeRole from string
static NodeRole parse_node_role(const std::string& value) {
    std::string v = trim(value);
    if (v == "\"ORIGIN\"" || v == "'ORIGIN'" || v == "ORIGIN") return NodeRole::ORIGIN;
    if (v == "\"EDGE\"" || v == "'EDGE'" || v == "EDGE") return NodeRole::EDGE;
    throw std::runtime_error("Unknown node role: " + value);
}

ClusterConfig load_config(const std::string& config_path) {
    if (!file_exists(config_path)) {
        throw std::runtime_error("Config file not found: " + config_path);
    }

    std::ifstream file(config_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + config_path);
    }

    ClusterConfig config;
    config.election_timeout_ms = 250;
    config.heartbeat_interval_ms = 50;
    config.session_timeout_sec = 3600;
    config.max_concurrent_sessions = 10000;
    config.expiry_scan_interval_sec = 300;
    config.compaction_threshold = 0.3f;
    config.heartbeat_interval_sec = 5;

    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        line = trim(line);

        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        // Parse section headers [section_name]
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            current_section = line.substr(1, line.length() - 2);
            current_section = trim(current_section);
            continue;
        }

        // Parse key = value pairs
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        // Remove trailing comments
        size_t comment_pos = value.find('#');
        if (comment_pos != std::string::npos) {
            value = trim(value.substr(0, comment_pos));
        }

        // Process by section
        if (current_section == "tls") {
            if (key == "ca_cert_file") config.tls.ca_cert_file = parse_string(value);
            if (key == "engine_cert_file") config.tls.engine_cert_file = parse_string(value);
            if (key == "engine_key_file") config.tls.engine_key_file = parse_string(value);
        } else if (current_section == "raft") {
            if (key == "election_timeout_ms") config.election_timeout_ms = parse_uint32(value);
            if (key == "heartbeat_interval_ms") config.heartbeat_interval_ms = parse_uint32(value);
        } else if (current_section == "session") {
            if (key == "timeout_sec") config.session_timeout_sec = parse_uint32(value);
            if (key == "max_concurrent") config.max_concurrent_sessions = parse_uint32(value);
        } else if (current_section == "expiry") {
            if (key == "scan_interval_sec") config.expiry_scan_interval_sec = parse_uint32(value);
            if (key == "compaction_threshold") config.compaction_threshold = parse_float(value);
        } else if (current_section == "heartbeat") {
            if (key == "interval_sec") config.heartbeat_interval_sec = parse_uint32(value);
        }
    }

    // TODO: Parse [registry_nodes], [storage_nodes], [groups], [tables], [api_keys]
    // For now, return a minimal valid config. Full parsing would use cpptoml library.

    // Validation
    if (config.tls.ca_cert_file.empty()) {
        throw std::runtime_error("Missing [tls] ca_cert_file");
    }
    if (config.registry_nodes.empty()) {
        throw std::runtime_error("At least one registry node required");
    }
    if (config.storage_nodes.empty()) {
        throw std::runtime_error("At least one storage node required");
    }
    if (config.groups.empty()) {
        throw std::runtime_error("At least one file group required");
    }

    return config;
}

}  // namespace filegroup
