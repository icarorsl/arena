#include <gtest/gtest.h>
#include <cstring>

#include "encryption/encryption.h"

namespace filegroup {

// Test key: 32 bytes of 0x01
static const uint8_t TEST_KEY[32] = {
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
};

TEST(EncryptionTest, NonceDeterministic) {
    auto n1 = derive_nonce(1, 0, 100);
    auto n2 = derive_nonce(1, 0, 100);
    EXPECT_EQ(n1, n2);
}

TEST(EncryptionTest, NonceDiffersByFileId) {
    auto n1 = derive_nonce(1, 0, 100);
    auto n2 = derive_nonce(2, 0, 100);
    EXPECT_NE(n1, n2);
}

TEST(EncryptionTest, NonceDiffersByChunkIndex) {
    auto n1 = derive_nonce(1, 0, 100);
    auto n2 = derive_nonce(1, 1, 100);
    EXPECT_NE(n1, n2);
}

TEST(EncryptionTest, NonceDiffersByGroupId) {
    auto n1 = derive_nonce(1, 0, 100);
    auto n2 = derive_nonce(1, 0, 200);
    EXPECT_NE(n1, n2);
}

TEST(EncryptionTest, EncryptDecryptRoundTrip) {
    uint8_t plaintext[256];
    for (int i = 0; i < 256; i++) plaintext[i] = (uint8_t)(i * 7);

    auto nonce = derive_nonce(42, 3, 100);
    auto ciphertext = encrypt_chunk(TEST_KEY, nonce, plaintext, sizeof(plaintext));

    // ciphertext = encrypted data + 16-byte tag
    EXPECT_GT(ciphertext.size(), sizeof(plaintext));
    EXPECT_EQ(ciphertext.size(), sizeof(plaintext) + 16);

    auto decrypted = decrypt_chunk(TEST_KEY, nonce, ciphertext.data(), ciphertext.size());
    EXPECT_EQ(decrypted.size(), sizeof(plaintext));
    EXPECT_EQ(std::memcmp(decrypted.data(), plaintext, sizeof(plaintext)), 0);
}

TEST(EncryptionTest, SameInputsProduceSameCiphertext) {
    uint8_t plaintext[64] = {};
    auto nonce = derive_nonce(1, 0, 0);
    auto c1 = encrypt_chunk(TEST_KEY, nonce, plaintext, 64);
    auto c2 = encrypt_chunk(TEST_KEY, nonce, plaintext, 64);
    EXPECT_EQ(c1, c2);
}

TEST(EncryptionTest, AuthTagMismatchThrows) {
    uint8_t plaintext[100] = {};
    auto nonce = derive_nonce(1, 0, 0);
    auto ciphertext = encrypt_chunk(TEST_KEY, nonce, plaintext, 100);

    // Corrupt the auth tag (last byte)
    ciphertext.back() ^= 0xFF;

    EXPECT_THROW(decrypt_chunk(TEST_KEY, nonce, ciphertext.data(), ciphertext.size()),
                 std::runtime_error);
}

TEST(EncryptionTest, WrongKeyFails) {
    uint8_t wrong_key[32] = {0x02};
    uint8_t plaintext[64] = {};
    auto nonce = derive_nonce(1, 0, 0);
    auto ciphertext = encrypt_chunk(TEST_KEY, nonce, plaintext, 64);

    EXPECT_THROW(decrypt_chunk(wrong_key, nonce, ciphertext.data(), ciphertext.size()),
                 std::runtime_error);
}

TEST(EncryptionTest, WrongNonceFails) {
    uint8_t plaintext[64] = {};
    auto nonce = derive_nonce(1, 0, 0);
    auto wrong_nonce = derive_nonce(2, 0, 0);
    auto ciphertext = encrypt_chunk(TEST_KEY, nonce, plaintext, 64);

    EXPECT_THROW(decrypt_chunk(TEST_KEY, wrong_nonce, ciphertext.data(), ciphertext.size()),
                 std::runtime_error);
}

TEST(EncryptionTest, EmptyPlaintext) {
    auto nonce = derive_nonce(1, 0, 0);
    auto ciphertext = encrypt_chunk(TEST_KEY, nonce, nullptr, 0);
    EXPECT_EQ(ciphertext.size(), 16); // Only the tag

    auto decrypted = decrypt_chunk(TEST_KEY, nonce, ciphertext.data(), ciphertext.size());
    EXPECT_TRUE(decrypted.empty());
}

}  // namespace filegroup
