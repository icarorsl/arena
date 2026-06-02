#include "encryption/encryption.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <stdexcept>
#include <cstring>

namespace filegroup {

// Default salt for PBKDF2 (128 bits)
const uint8_t DEFAULT_SALT[] = {
    0x50, 0x68, 0x61, 0x73, 0x65, 0x31, 0x46, 0x69,
    0x6c, 0x65, 0x47, 0x72, 0x6f, 0x75, 0x70, 0x00
};
const size_t DEFAULT_SALT_SIZE = sizeof(DEFAULT_SALT);

std::string derive_key(
    const std::string& passphrase,
    const std::string& salt,
    uint32_t iterations) {
    
    if (passphrase.empty()) {
        throw std::runtime_error("passphrase is empty");
    }
    
    uint8_t key_buffer[AES256_KEY_SIZE];
    
    // Use provided salt or default
    const uint8_t* salt_ptr = DEFAULT_SALT;
    size_t salt_len = DEFAULT_SALT_SIZE;
    
    if (!salt.empty()) {
        salt_ptr = reinterpret_cast<const uint8_t*>(salt.data());
        salt_len = salt.size();
    }
    
    // PBKDF2-SHA256
    int rc = PKCS5_PBKDF2_HMAC(
        passphrase.data(),
        passphrase.size(),
        salt_ptr,
        salt_len,
        iterations,
        EVP_sha256(),
        AES256_KEY_SIZE,
        key_buffer
    );
    
    if (rc != 1) {
        throw std::runtime_error("PBKDF2 key derivation failed");
    }
    
    return std::string(reinterpret_cast<const char*>(key_buffer), AES256_KEY_SIZE);
}

std::string generate_iv() {
    uint8_t iv_buffer[AES256_IV_SIZE];
    
    if (RAND_bytes(iv_buffer, AES256_IV_SIZE) != 1) {
        throw std::runtime_error("Failed to generate random IV");
    }
    
    return std::string(reinterpret_cast<const char*>(iv_buffer), AES256_IV_SIZE);
}

std::string encrypt(
    const std::string& plaintext,
    const std::string& key,
    const std::string& additional_data) {
    
    if (key.size() != AES256_KEY_SIZE) {
        throw std::runtime_error("key size must be " + std::to_string(AES256_KEY_SIZE) + " bytes");
    }
    
    // Generate random IV
    std::string iv = generate_iv();
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }
    
    try {
        // Initialize encryption
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, 
                              reinterpret_cast<const uint8_t*>(key.data()),
                              reinterpret_cast<const uint8_t*>(iv.data())) != 1) {
            throw std::runtime_error("EVP_EncryptInit_ex failed");
        }
        
        // Process additional data if provided
        if (!additional_data.empty()) {
            int len = 0;
            if (EVP_EncryptUpdate(ctx, nullptr, &len,
                                 reinterpret_cast<const uint8_t*>(additional_data.data()),
                                 additional_data.size()) != 1) {
                throw std::runtime_error("Failed to process additional data");
            }
        }
        
        // Encrypt plaintext
        int ciphertext_len = 0;
        std::vector<uint8_t> ciphertext_buffer(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
        
        if (EVP_EncryptUpdate(ctx, ciphertext_buffer.data(), &ciphertext_len,
                             reinterpret_cast<const uint8_t*>(plaintext.data()),
                             plaintext.size()) != 1) {
            throw std::runtime_error("EVP_EncryptUpdate failed");
        }
        
        // Finalize
        int final_len = 0;
        if (EVP_EncryptFinal_ex(ctx, ciphertext_buffer.data() + ciphertext_len, &final_len) != 1) {
            throw std::runtime_error("EVP_EncryptFinal_ex failed");
        }
        ciphertext_len += final_len;
        
        // Get authentication tag
        uint8_t tag_buffer[AES256_TAG_SIZE];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES256_TAG_SIZE, tag_buffer) != 1) {
            throw std::runtime_error("Failed to get authentication tag");
        }
        
        // Build output: IV || tag || ciphertext
        std::string result;
        result.reserve(iv.size() + AES256_TAG_SIZE + ciphertext_len);
        result.append(iv);
        result.append(reinterpret_cast<const char*>(tag_buffer), AES256_TAG_SIZE);
        result.append(reinterpret_cast<const char*>(ciphertext_buffer.data()), ciphertext_len);
        
        return result;
        
    } catch (...) {
        EVP_CIPHER_CTX_free(ctx);
        throw;
    }
    
    EVP_CIPHER_CTX_free(ctx);
}

std::string decrypt(
    const std::string& ciphertext,
    const std::string& key,
    const std::string& additional_data) {
    
    if (key.size() != AES256_KEY_SIZE) {
        throw std::runtime_error("key size must be " + std::to_string(AES256_KEY_SIZE) + " bytes");
    }
    
    if (ciphertext.size() < AES256_IV_SIZE + AES256_TAG_SIZE) {
        throw std::runtime_error("ciphertext too short (must have IV + tag)");
    }
    
    // Extract IV, tag, and encrypted data
    const std::string iv = ciphertext.substr(0, AES256_IV_SIZE);
    const std::string tag = ciphertext.substr(AES256_IV_SIZE, AES256_TAG_SIZE);
    const std::string encrypted = ciphertext.substr(AES256_IV_SIZE + AES256_TAG_SIZE);
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }
    
    try {
        // Initialize decryption
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                              reinterpret_cast<const uint8_t*>(key.data()),
                              reinterpret_cast<const uint8_t*>(iv.data())) != 1) {
            throw std::runtime_error("EVP_DecryptInit_ex failed");
        }
        
        // Process additional data if provided
        if (!additional_data.empty()) {
            int len = 0;
            if (EVP_DecryptUpdate(ctx, nullptr, &len,
                                 reinterpret_cast<const uint8_t*>(additional_data.data()),
                                 additional_data.size()) != 1) {
                throw std::runtime_error("Failed to process additional data");
            }
        }
        
        // Set authentication tag before decryption
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES256_TAG_SIZE,
                               const_cast<char*>(tag.data())) != 1) {
            throw std::runtime_error("Failed to set authentication tag");
        }
        
        // Decrypt ciphertext
        int plaintext_len = 0;
        std::vector<uint8_t> plaintext_buffer(encrypted.size() + EVP_MAX_BLOCK_LENGTH);
        
        if (EVP_DecryptUpdate(ctx, plaintext_buffer.data(), &plaintext_len,
                             reinterpret_cast<const uint8_t*>(encrypted.data()),
                             encrypted.size()) != 1) {
            throw std::runtime_error("EVP_DecryptUpdate failed");
        }
        
        // Finalize (verifies tag)
        int final_len = 0;
        if (EVP_DecryptFinal_ex(ctx, plaintext_buffer.data() + plaintext_len, &final_len) != 1) {
            throw std::runtime_error("Decryption failed: authentication tag mismatch or corrupted data");
        }
        plaintext_len += final_len;
        
        std::string result(reinterpret_cast<const char*>(plaintext_buffer.data()), plaintext_len);
        return result;
        
    } catch (...) {
        EVP_CIPHER_CTX_free(ctx);
        throw;
    }
    
    EVP_CIPHER_CTX_free(ctx);
}

size_t encrypt_to_buffer(
    const std::string& plaintext,
    const std::string& key,
    std::vector<uint8_t>& output,
    const std::string& additional_data) {
    
    std::string encrypted = encrypt(plaintext, key, additional_data);
    output.assign(encrypted.begin(), encrypted.end());
    return output.size();
}

size_t decrypt_from_buffer(
    const std::vector<uint8_t>& ciphertext,
    const std::string& key,
    std::vector<uint8_t>& output,
    const std::string& additional_data) {
    
    std::string ciphertext_str(ciphertext.begin(), ciphertext.end());
    std::string decrypted = decrypt(ciphertext_str, key, additional_data);
    output.assign(decrypted.begin(), decrypted.end());
    return output.size();
}

}  // namespace filegroup
