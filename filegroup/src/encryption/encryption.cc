#include "encryption/encryption.h"

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <cstring>
#include <stdexcept>

namespace filegroup {

// ============================================================================
// Nonce Derivation
// ============================================================================

std::array<uint8_t, AES256_NONCE_SIZE> derive_nonce(
    uint64_t file_id, uint32_t chunk_index, uint32_t group_id) {

    // Build input: file_id (8 bytes LE) || chunk_index (4 bytes LE) || group_id (4 bytes LE)
    uint8_t input[16];
    std::memcpy(input, &file_id, 8);
    std::memcpy(input + 8, &chunk_index, 4);
    std::memcpy(input + 12, &group_id, 4);

    // SHA-256 hash
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(input, sizeof(input), hash);

    // Use first 12 bytes as nonce
    std::array<uint8_t, AES256_NONCE_SIZE> nonce;
    std::memcpy(nonce.data(), hash, AES256_NONCE_SIZE);
    return nonce;
}

// ============================================================================
// AES-256-GCM Encrypt
// ============================================================================

std::vector<uint8_t> encrypt_chunk(
    const uint8_t* key,
    const std::array<uint8_t, AES256_NONCE_SIZE>& nonce,
    const uint8_t* plaintext, size_t plaintext_len) {

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create cipher context");

    // Initialize encryption with AES-256-GCM
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    }

    // Set IV (nonce) length to 12 bytes
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES256_NONCE_SIZE, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set GCM IV length");
    }

    // Set key and nonce
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set key and IV");
    }

    // Allocate output: ciphertext + 16-byte tag
    std::vector<uint8_t> output(plaintext_len + AES256_TAG_SIZE);

    // Encrypt
    int out_len = 0;
    if (EVP_EncryptUpdate(ctx, output.data(), &out_len, plaintext, static_cast<int>(plaintext_len)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }

    int total_len = out_len;

    // Finalize — writes nothing extra for GCM stream, but needed to flush
    if (EVP_EncryptFinal_ex(ctx, output.data() + total_len, &out_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }
    total_len += out_len;

    // Get the 16-byte GCM auth tag and append it after ciphertext
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES256_TAG_SIZE,
                              output.data() + total_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to get GCM tag");
    }

    EVP_CIPHER_CTX_free(ctx);
    return output; // ciphertext + tag (same as total_len + 16)
}

// ============================================================================
// AES-256-GCM Decrypt
// ============================================================================

std::vector<uint8_t> decrypt_chunk(
    const uint8_t* key,
    const std::array<uint8_t, AES256_NONCE_SIZE>& nonce,
    const uint8_t* ciphertext_with_tag, size_t total_len) {

    if (total_len < AES256_TAG_SIZE) {
        throw std::runtime_error("Ciphertext too short: missing auth tag");
    }

    size_t ciphertext_len = total_len - AES256_TAG_SIZE;
    const uint8_t* tag = ciphertext_with_tag + ciphertext_len;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create cipher context");

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptInit_ex failed");
    }

    // Set IV length
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES256_NONCE_SIZE, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set GCM IV length");
    }

    // Set key and nonce
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set key and IV");
    }

    // Set the expected auth tag BEFORE decryption (critical for GCM verification)
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES256_TAG_SIZE,
                              const_cast<uint8_t*>(tag)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set GCM tag");
    }

    // Decrypt
    std::vector<uint8_t> plaintext(ciphertext_len);
    int out_len = 0;
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &out_len, ciphertext_with_tag,
                           static_cast<int>(ciphertext_len)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptUpdate failed");
    }

    int total_out = out_len;

    // Finalize — this is where GCM auth tag verification happens
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + total_out, &out_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("GCM authentication failed: tag mismatch or data corruption");
    }

    total_out += out_len;
    plaintext.resize(total_out);

    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

}  // namespace filegroup
