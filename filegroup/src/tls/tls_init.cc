#include "tls/tls_init.h"

#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <fstream>
#include <sstream>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdexcept>
#include <cstring>

namespace filegroup {

// Helper: create directory if it doesn't exist
static void ensure_dir(const std::string& path) {
    mkdir(path.c_str(), 0755);
}

// Helper: write PEM file with restricted permissions
static void write_pem_file(const std::string& path, bool is_private, const std::string& content) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open file for writing: " + path);
    }
    file << content;
    file.close();
    
    // Set permissions: 0644 for certs, 0400 for keys
    int mode = is_private ? 0400 : 0644;
    chmod(path.c_str(), mode);
}

// Helper: generate RSA key pair
static EVP_PKEY* generate_key() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) throw std::runtime_error("Failed to create key context");
    
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize key generation");
    }
    
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("Failed to set RSA key bits");
    }
    
    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("Failed to generate key");
    }
    
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

// Helper: create self-signed certificate
static X509* create_self_signed_cert(EVP_PKEY* pkey, const std::string& common_name, int days) {
    X509* cert = X509_new();
    if (!cert) throw std::runtime_error("Failed to create certificate");
    
    // Set serial number
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    
    // Set validity
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), days * 86400L);
    
    // Set public key
    if (!X509_set_pubkey(cert, pkey)) {
        X509_free(cert);
        throw std::runtime_error("Failed to set public key");
    }
    
    // Set subject and issuer name
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASN1, (unsigned char*)"US", -1, -1);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASN1, (unsigned char*)"FileGroup", -1, -1);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASN1, (unsigned char*)common_name.c_str(), -1, -1);
    X509_set_issuer_name(cert, name);
    
    // Add extensions for CA cert
    if (common_name.find("CA") != std::string::npos) {
        X509V3_CTX ctx;
        X509V3_CTX_init(&ctx);
        X509V3_CTX_set_cert(&ctx, cert, cert, nullptr);
        
        X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:TRUE");
        if (ext) X509_add_extensions(cert, &ext, 1);
    }
    
    // Sign the certificate with its own key
    if (!X509_sign(cert, pkey, EVP_sha256())) {
        X509_free(cert);
        throw std::runtime_error("Failed to sign certificate");
    }
    
    return cert;
}

// Helper: write X509 certificate to PEM string
static std::string cert_to_pem(X509* cert) {
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio || !PEM_write_bio_X509(bio, cert)) {
        if (bio) BIO_free(bio);
        throw std::runtime_error("Failed to write certificate to PEM");
    }
    
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);
    return result;
}

// Helper: write EVP_PKEY to PEM string
static std::string key_to_pem(EVP_PKEY* pkey) {
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio || !PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
        if (bio) BIO_free(bio);
        throw std::runtime_error("Failed to write key to PEM");
    }
    
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);
    return result;
}

std::string tls_init(const TLSInitOptions& options) {
    ensure_dir(options.output_dir);
    
    // Generate CA key and certificate
    EVP_PKEY* ca_key = generate_key();
    X509* ca_cert = create_self_signed_cert(ca_key, "filegroup-cluster-CA", options.ca_days);
    
    std::string ca_cert_pem = cert_to_pem(ca_cert);
    std::string ca_key_pem = key_to_pem(ca_key);
    
    // Write CA files
    std::string ca_cert_path = options.output_dir + "/ca.crt";
    std::string ca_key_path = options.output_dir + "/ca.key";
    write_pem_file(ca_cert_path, false, ca_cert_pem);
    write_pem_file(ca_key_path, true, ca_key_pem);
    
    // Generate engine certificate
    EVP_PKEY* engine_key = generate_key();
    X509* engine_cert = create_self_signed_cert(engine_key, "filegroup-engine", options.cert_days);
    
    std::string engine_cert_pem = cert_to_pem(engine_cert);
    std::string engine_key_pem = key_to_pem(engine_key);
    
    std::string engine_cert_path = options.output_dir + "/engine.crt";
    std::string engine_key_path = options.output_dir + "/engine.key";
    write_pem_file(engine_cert_path, false, engine_cert_pem);
    write_pem_file(engine_key_path, true, engine_key_pem);
    
    // Generate registry node certificates
    for (uint32_t i = 0; i < 3; ++i) {
        EVP_PKEY* node_key = generate_key();
        std::string node_name = "filegroup-registry-" + std::to_string(i);
        X509* node_cert = create_self_signed_cert(node_key, node_name, options.cert_days);
        
        std::string node_cert_pem = cert_to_pem(node_cert);
        std::string node_key_pem = key_to_pem(node_key);
        
        std::string cert_path = options.output_dir + "/registry_" + std::to_string(i) + ".crt";
        std::string key_path = options.output_dir + "/registry_" + std::to_string(i) + ".key";
        write_pem_file(cert_path, false, node_cert_pem);
        write_pem_file(key_path, true, node_key_pem);
        
        X509_free(node_cert);
        EVP_PKEY_free(node_key);
    }
    
    // Generate storage node certificates
    for (uint32_t i = 0; i < options.num_nodes; ++i) {
        EVP_PKEY* node_key = generate_key();
        std::string node_name = "filegroup-storage-" + std::to_string(i);
        X509* node_cert = create_self_signed_cert(node_key, node_name, options.cert_days);
        
        std::string node_cert_pem = cert_to_pem(node_cert);
        std::string node_key_pem = key_to_pem(node_key);
        
        std::string cert_path = options.output_dir + "/storage_" + std::to_string(i) + ".crt";
        std::string key_path = options.output_dir + "/storage_" + std::to_string(i) + ".key";
        write_pem_file(cert_path, false, node_cert_pem);
        write_pem_file(key_path, true, node_key_pem);
        
        X509_free(node_cert);
        EVP_PKEY_free(node_key);
    }
    
    // Clean up
    X509_free(ca_cert);
    X509_free(engine_cert);
    EVP_PKEY_free(ca_key);
    EVP_PKEY_free(engine_key);
    
    // Generate TOML snippet
    std::stringstream ss;
    ss << "[tls]\n";
    ss << "ca_cert_file = \"" << ca_cert_path << "\"\n";
    ss << "engine_cert_file = \"" << engine_cert_path << "\"\n";
    ss << "engine_key_file = \"" << engine_key_path << "\"\n";
    
    return ss.str();
}

}  // namespace filegroup
