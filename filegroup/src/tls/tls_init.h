#pragma once

#include <string>
#include <vector>

namespace filegroup {

struct TLSInitOptions {
    uint32_t num_nodes;          // Number of storage nodes (e.g., 3)
    std::string output_dir;      // Directory to write certs and keys
    uint32_t ca_days = 3650;     // CA certificate validity (years)
    uint32_t cert_days = 365;    // Node certificate validity (years)
    std::string common_name = "filegroup-cluster";  // Cluster name
};

/**
 * Initialize TLS certificates for the cluster.
 * Generates:
 * - Cluster root CA certificate and key
 * - Engine certificate and key
 * - Registry node certificates and keys
 * - Storage node certificates and keys
 *
 * @param options initialization options
 * @return TOML configuration snippet for [tls] section
 */
std::string tls_init(const TLSInitOptions& options);

}  // namespace filegroup
