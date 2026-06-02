#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace filegroup {

/**
 * AES-256-GCM encryption for Phase 1.
 *
 * Provides authenticated encryption using OpenSSL EVP API.
 * Each message has a unique IV (nonce), authentication tag, and ciphertext.
 */

const size_t AES256_KEY_SIZE = 32;      // 256 bits
const size_t AES256_IV_SIZE = 12;       // 96 bits (GCM standard)
const size_t AES256_TAG_SIZE = 16;      // 128 bits authentication tag

/**
 * Derives an AES-256 key from a passphrase using PBKDF2-SHA256.
 *
 * Parameters:
 *   passphrase: User input password/passphrase
 *   salt: Optional salt for key derivation (if empty, uses default)
 *   iterations: PBKDF2 iteration count (default: 100000)
 *
 * Returns:
 *   32-byte key suitable for AES-256 operations
 *
 * Throws:
 *   std::runtime_error if OpenSSL operations fail
 */
std::string derive_key(
    const std::string& passphrase,
    const std::string& salt = "",
    uint32_t iterations = 100000
);

/**
 * Generates a random 12-byte IV (nonce) for GCM mode.
 *
 * Returns:
 *   12-byte random IV
 *
 * Throws:
 *   std::runtime_error if random generation fails
 */
std::string generate_iv();

/**
 * Encrypts plaintext using AES-256-GCM.
 *
 * Output format:
 *   [12-byte IV][16-byte tag][ciphertext]
 *
 * Parameters:
 *   plaintext: Data to encrypt
 *   key: 32-byte AES-256 key (from derive_key or manually)
 *   additional_data: Optional AAD for authentication (default: empty)
 *
 * Returns:
 *   Encrypted data with IV and authentication tag prepended
 *
 * Throws:
 *   std::runtime_error if encryption fails
 */
std::string encrypt(
    const std::string& plaintext,
    const std::string& key,
    const std::string& additional_data = ""
);

/**
 * Decrypts ciphertext encrypted with AES-256-GCM.
 *
 * Input format:
 *   [12-byte IV][16-byte tag][ciphertext]
 *
 * Parameters:
 *   ciphertext: Data from encrypt() call
 *   key: Same 32-byte key used in encrypt()
 *   additional_data: Same AAD used during encryption
 *
 * Returns:
 *   Decrypted plaintext
 *
 * Throws:
 *   std::runtime_error if decryption fails or authentication tag is invalid
 */
std::string decrypt(
    const std::string& ciphertext,
    const std::string& key,
    const std::string& additional_data = ""
);

/**
 * Encrypts plaintext in-place, appending IV and tag to output.
 *
 * Variant for performance-critical code where output buffer is pre-allocated.
 *
 * Parameters:
 *   plaintext: Data to encrypt
 *   key: 32-byte AES-256 key
 *   output: Output vector (will be cleared and resized)
 *   additional_data: Optional AAD
 *
 * Returns:
 *   Number of bytes written to output
 *
 * Throws:
 *   std::runtime_error if encryption fails
 */
size_t encrypt_to_buffer(
    const std::string& plaintext,
    const std::string& key,
    std::vector<uint8_t>& output,
    const std::string& additional_data = ""
);

/**
 * Decrypts ciphertext in-place from buffer.
 *
 * Variant for performance-critical code where ciphertext is in a vector.
 *
 * Parameters:
 *   ciphertext: Data from encrypt_to_buffer() call
 *   key: Same 32-byte key used during encryption
 *   output: Output vector (will be cleared and resized)
 *   additional_data: Same AAD used during encryption
 *
 * Returns:
 *   Number of bytes written to output
 *
 * Throws:
 *   std::runtime_error if decryption fails or authentication tag is invalid
 */
size_t decrypt_from_buffer(
    const std::vector<uint8_t>& ciphertext,
    const std::string& key,
    std::vector<uint8_t>& output,
    const std::string& additional_data = ""
);

}  // namespace filegroup
