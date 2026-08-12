#include "crypto/Hash.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <random>
#include <set>
#include <string>
#include <iostream>

using namespace forgechain::crypto;

namespace {

bytes toBytes(const std::string& s) {
    return bytes(s.begin(), s.end());
}

bytes randomBytes(size_t len, std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, 255);
    bytes out(len);
    for (auto& b : out) {
        b = static_cast<uint8_t>(dist(rng));
    }
    return out;
}

}  // namespace



TEST(Sha256, EmptyString) {
    auto hash = sha256(toBytes(""));
    EXPECT_EQ(to_hex(hash),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, KnownVectorAbc) {
    auto hash = sha256(toBytes("abc"));
    EXPECT_EQ(to_hex(hash),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, ReturnsExactly32Bytes) {
    auto hash = sha256(toBytes("forgechain"));
    EXPECT_EQ(hash.size(), 32u);
}



TEST(Sha256, IsDeterministicAcrossManyCalls) {
    const bytes input = toBytes("forgechain genesis block");
    const auto first = sha256(input);

    for (int i = 0; i < 10000; ++i) {
        EXPECT_EQ(sha256(input), first) << "mismatch at iteration " << i;
    }
}

TEST(Sha256, SingleBitFlipChangesHashCompletely) {
    bytes a = toBytes("forgechain");
    bytes b = a;
    b[0] ^= 0x01;

    auto hashA = sha256(a);
    auto hashB = sha256(b);

    EXPECT_NE(hashA, hashB);


    int differingBits = 0;
    for (size_t i = 0; i < hashA.size(); ++i) {
        differingBits += std::popcount(static_cast<unsigned>(hashA[i] ^ hashB[i]));
    }
    EXPECT_GT(differingBits, 64);
    EXPECT_LT(differingBits, 192);
}

TEST(Sha256, EmptyAndNonEmptyDiffer) {
    auto emptyHash = sha256(toBytes(""));
    auto nonEmptyHash = sha256(toBytes("a"));
    EXPECT_NE(emptyHash, nonEmptyHash);
}

TEST(Sha256, DifferentLengthsWithSamePrefixDiffer) {
    auto a = sha256(toBytes("forgechain"));
    auto b = sha256(toBytes("forgechain1"));
    EXPECT_NE(a, b);
}

TEST(Sha256, ManyRandomInputsProduceValidUniqueHashes) {
    std::mt19937 rng(42);
    std::set<std::string> seenHashes;

    constexpr int kIterations = 5000;
    for (int i = 0; i < kIterations; ++i) {

        size_t len = 1 + static_cast<size_t>(rng() % 255);
        auto input = randomBytes(len, rng);
        auto hash = sha256(input);

        ASSERT_EQ(hash.size(), 32u);
        seenHashes.insert(to_hex(hash));
    }

    EXPECT_EQ(seenHashes.size(), static_cast<size_t>(kIterations));
}

TEST(Sha256, HandlesLargeInput) {
    bytes large(1'000'000, 0xAB);
    auto hash = sha256(large);
    EXPECT_EQ(hash.size(), 32u);
}

TEST(Sha256, HandlesEmptyVectorWithoutCrashing) {
    bytes empty;
    EXPECT_NO_THROW({
        auto hash = sha256(empty);
        EXPECT_EQ(hash.size(), 32u);
    });
}


TEST(DoubleSha256, EqualsSha256AppliedTwice) {
    const bytes input = toBytes("block header bytes");
    auto once = sha256(input);
    bytes onceAsBytes(once.begin(), once.end());
    auto expected = sha256(onceAsBytes);

    auto actual = double_sha_256(input);

    EXPECT_EQ(actual, expected);
}

TEST(DoubleSha256, DiffersFromSingleSha256) {
    const bytes input = toBytes("block header bytes");
    auto single = sha256(input);
    auto doubled = double_sha_256(input);
    EXPECT_NE(single, doubled);
}

TEST(DoubleSha256, IsDeterministicAcrossManyCalls) {
    const bytes input = toBytes("forgechain genesis block");
    const auto first = double_sha_256(input);

    for (int i = 0; i < 10000; ++i) {
        EXPECT_EQ(double_sha_256(input), first) << "mismatch at iteration " << i;
    }
}

TEST(DoubleSha256, ManyRandomInputsProduceUniqueHashes) {
    std::mt19937 rng(123);
    std::set<std::string> seenHashes;

    constexpr int kIterations = 5000;
    for (int i = 0; i < kIterations; ++i) {

        auto input = randomBytes(16, rng);
        auto hash = double_sha_256(input);
        seenHashes.insert(to_hex(hash));
    }

    EXPECT_EQ(seenHashes.size(), static_cast<size_t>(kIterations));
}



TEST(ToHex, ProducesLowercase64CharString) {
    auto hash = sha256(toBytes("forgechain"));
    auto hex = to_hex(hash);

    EXPECT_EQ(hex.size(), 64u);
    EXPECT_TRUE(std::all_of(hex.begin(), hex.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }));
}

TEST(ToHex, AllZeroBytesProduceAllZeroHex) {
    HashBytes zero{};  // value-initialized: all bytes are 0
    EXPECT_EQ(to_hex(zero), std::string(64, '0'));
}

TEST(ToHex, AllMaxBytesProduceAllFHex) {
    HashBytes maxed;
    maxed.fill(0xFF);
    EXPECT_EQ(to_hex(maxed), std::string(64, 'f'));
}

TEST(ToHex, KnownByteSequence) {
    HashBytes hash{};
    hash[0] = 0xDE;
    hash[1] = 0xAD;
    hash[2] = 0xBE;
    hash[3] = 0xEF;


    auto hex = to_hex(hash);
    EXPECT_EQ(hex.substr(0, 8), "deadbeef");
    EXPECT_EQ(hex.substr(8), std::string(56, '0'));
}

TEST(ToHex, IsDeterministic) {
    auto hash = sha256(toBytes("determinism check"));
    EXPECT_EQ(to_hex(hash), to_hex(hash));
}

TEST(ToHex, RoundTripsSha256KnownVector) {
    auto hash = sha256(toBytes(""));
    EXPECT_EQ(to_hex(hash),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}


TEST(Sha256, StressManyDistinctInputs) {
    std::mt19937 rng(7);
    constexpr int kIterations = 100000;

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        auto input = randomBytes(64, rng);
        auto hash = sha256(input);
        ASSERT_EQ(hash.size(), 32u);
    }
    auto elapsed = std::chrono::steady_clock::now() - start;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::cout << "[ INFO     ] " << kIterations << " sha256 calls took " << ms
              << " ms (" << (static_cast<double>(ms) / kIterations) << " ms/call)\n";
}
