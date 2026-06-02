#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace filegroup {

constexpr size_t AES256_KEY_SIZE = 32;
constexpr size_t AES256_NONCE_SIZE = 12;
constexpr size_t AES256_TAG_SIZE = 16;

std::array<uint8_t, AES256_NONCE_SIZE> derive_nonce(
    uint64_t file_id, uint32_t chunk_index, uint32_t group_id);

std::vector<uint8_t> encrypt_chunk(
    const uint8_t* key,
    const std::array<uint8_t, AES256_NONCE_SIZE>& nonce,
    const uint8_t* plaintext, size_t plaintext_len);

std::vector<uint8_t> decrypt_chunk(
    const uint8_t* key,
    const std::array<uint8_t, AES256_NONCE_SIZE>& nonce,
    const uint8_t* ciphertext_with_tag, size_t total_len);

}  // namespace filegroup
