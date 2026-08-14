#include "crypto/Keys.hpp"
#include "crypto/Hash.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <cstdint>
#include <cstddef>
using namespace forgechain::crypto;

namespace {

std::string toHexString(const bytes& data) {
    static const char* hexChars = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        result += hexChars[(byte >> 4) & 0x0F];
        result += hexChars[byte & 0x0F];
    }
    return result;
}

bool isAllZero(const bytes& data) {
    return std::all_of(data.begin(), data.end(), [](uint8_t b) { return b == 0; });
}

}

TEST(GenerateKeypair, ProducesNonEmptyKeys) {
    KeyPair kp = generate_keypair();
    EXPECT_FALSE(kp.private_key.empty());
    EXPECT_FALSE(kp.public_key.empty());
}

TEST(GenerateKeypair, PrivateKeyIsAtMost32BytesForSecp256k1) {
    KeyPair kp = generate_keypair();
    EXPECT_LE(kp.private_key.size(), 32u);
    EXPECT_GT(kp.private_key.size(), 0u);
}

TEST(GenerateKeypair, PublicKeyIsSixtyFiveBytesUncompressed) {
    KeyPair kp = generate_keypair();
    EXPECT_EQ(kp.public_key.size(), 65u);
}

TEST(GenerateKeypair, UncompressedPublicKeyStartsWithCorrectPrefix) {
    KeyPair kp = generate_keypair();
    ASSERT_FALSE(kp.public_key.empty());
    EXPECT_EQ(kp.public_key[0], 0x04);
}

TEST(GenerateKeypair, TwoCallsProduceDifferentPrivateKeys) {
    KeyPair a = generate_keypair();
    KeyPair b = generate_keypair();
    EXPECT_NE(a.private_key, b.private_key);
}

TEST(GenerateKeypair, TwoCallsProduceDifferentPublicKeys) {
    KeyPair a = generate_keypair();
    KeyPair b = generate_keypair();
    EXPECT_NE(a.public_key, b.public_key);
}

TEST(GenerateKeypair, PrivateKeyIsNeverAllZero) {
    KeyPair kp = generate_keypair();
    EXPECT_FALSE(isAllZero(kp.private_key));
}

TEST(GenerateKeypair, ManyGeneratedKeypairsAreAllUnique) {
    std::set<std::string> seenPrivateKeys;

    constexpr int kIterations = 200;
    for (int i = 0; i < kIterations; ++i) {
        KeyPair kp = generate_keypair();
        seenPrivateKeys.insert(toHexString(kp.private_key));
    }

    EXPECT_EQ(seenPrivateKeys.size(), static_cast<size_t>(kIterations));
}

TEST(GenerateKeypair, PublicAndPrivateKeyAreConsistentAcrossManyPairs) {
    std::vector<KeyPair> pairs;
    constexpr int kIterations = 50;
    for (int i = 0; i < kIterations; ++i) {
        pairs.push_back(generate_keypair());
    }

    for (size_t i = 0; i < pairs.size(); ++i) {
        for (size_t j = i + 1; j < pairs.size(); ++j) {
            bool samePriv = (pairs[i].private_key == pairs[j].private_key);
            bool samePub = (pairs[i].public_key == pairs[j].public_key);
            EXPECT_EQ(samePriv, samePub)
                << "pair " << i << " and " << j
                << " disagree: private keys "
                << (samePriv ? "match" : "differ") << " but public keys "
                << (samePub ? "match" : "differ");
        }
    }
}

TEST(GenerateKeypair, RepeatedCallsRemainStable) {
    EXPECT_NO_THROW({
        for (int i = 0; i < 10000; ++i) {
            KeyPair kp = generate_keypair();
            ASSERT_FALSE(kp.private_key.empty()) << "failed at iteration " << i;
            ASSERT_FALSE(kp.public_key.empty()) << "failed at iteration " << i;
        }
    });
}
