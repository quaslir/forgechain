#include "core/Block.hpp"
#include "crypto/Hash.hpp"
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <cstdint>
#include <cstddef>

using namespace forgechain::core;
using forgechain::crypto::HashBytes;

namespace {

HashBytes zeroHash() {
    return HashBytes{};
}

HashBytes fakeHash(uint8_t seed) {
    HashBytes h{};
    h[0] = seed;
    return h;
}

}


TEST(Block, ConstructorStoresGivenFields) {
    HashBytes prevHash = fakeHash(0xAB);
    Block block(1, prevHash, 1700000000);

    EXPECT_EQ(block.version_, 1u);
    EXPECT_EQ(block.prev_hash_, prevHash);
    EXPECT_EQ(block.timestamp_, 1700000000u);
}

TEST(Block, ConstructorDefaultsNonceToZero) {
    Block block(1, zeroHash(), 1700000000);
    EXPECT_EQ(block.nonce_, 0u);
}

TEST(Block, ConstructorComputesNonEmptyHash) {
    Block block(1, zeroHash(), 1700000000);
    EXPECT_NE(block.hash_, zeroHash());
}


TEST(Block, SerializeIsDeterministic) {
    HashBytes prevHash = fakeHash(0x11);
    Block a(1, prevHash, 1700000000);
    Block b(1, prevHash, 1700000000);

    EXPECT_EQ(a.serialize(), b.serialize());
}

TEST(Block, SerializeIsNonEmpty) {
    Block block(1, zeroHash(), 1700000000);
    EXPECT_FALSE(block.serialize().empty());
}

TEST(Block, SerializeChangesWhenVersionChanges) {
    Block a(1, zeroHash(), 1700000000);
    Block b(2, zeroHash(), 1700000000);
    EXPECT_NE(a.serialize(), b.serialize());
}

TEST(Block, SerializeChangesWhenPrevHashChanges) {
    Block a(1, fakeHash(0x01), 1700000000);
    Block b(1, fakeHash(0x02), 1700000000);
    EXPECT_NE(a.serialize(), b.serialize());
}

TEST(Block, SerializeChangesWhenTimestampChanges) {
    Block a(1, zeroHash(), 1700000000);
    Block b(1, zeroHash(), 1700000001);
    EXPECT_NE(a.serialize(), b.serialize());
}


TEST(Block, IdenticalFieldsProduceIdenticalHash) {
    HashBytes prevHash = fakeHash(0x42);
    Block a(1, prevHash, 1700000000);
    Block b(1, prevHash, 1700000000);

    EXPECT_EQ(a.hash_, b.hash_);
}

TEST(Block, HashIsDeterministicAcrossManyConstructions) {
    HashBytes prevHash = fakeHash(0x99);
    Block first(1, prevHash, 1700000000);

    for (int i = 0; i < 1000; ++i) {
        Block again(1, prevHash, 1700000000);
        EXPECT_EQ(again.hash_, first.hash_) << "mismatch at iteration " << i;
    }
}

TEST(Block, DifferentVersionProducesDifferentHash) {
    Block a(1, zeroHash(), 1700000000);
    Block b(2, zeroHash(), 1700000000);
    EXPECT_NE(a.hash_, b.hash_);
}

TEST(Block, DifferentPrevHashProducesDifferentHash) {
    Block a(1, fakeHash(0x01), 1700000000);
    Block b(1, fakeHash(0x02), 1700000000);
    EXPECT_NE(a.hash_, b.hash_);
}

TEST(Block, DifferentTimestampProducesDifferentHash) {
    Block a(1, zeroHash(), 1700000000);
    Block b(1, zeroHash(), 1700000001);
    EXPECT_NE(a.hash_, b.hash_);
}

TEST(Block, HashMatchesRecomputationFromSerialize) {
    Block block(1, fakeHash(0x77), 1700000000);

    auto recomputed = forgechain::crypto::double_sha_256(block.serialize());
    EXPECT_EQ(block.hash_, recomputed);
}

TEST(Block, ManyDistinctBlocksProduceUniqueHashes) {
    std::set<std::string> seenHashes;

    constexpr int kIterations = 2000;
    for (int i = 0; i < kIterations; ++i) {
        HashBytes prevHash{};
        prevHash[0] = static_cast<uint8_t>(i & 0xFF);
        prevHash[1] = static_cast<uint8_t>((i >> 8) & 0xFF);

        Block block(static_cast<uint32_t>(i),
                    prevHash,
                    static_cast<uint64_t>(1700000000) + static_cast<uint64_t>(i));

        seenHashes.insert(forgechain::crypto::to_hex(block.hash_));
    }

    EXPECT_EQ(seenHashes.size(), static_cast<size_t>(kIterations));
}


TEST(Block, HandlesZeroTimestamp) {
    EXPECT_NO_THROW({
        Block block(1, zeroHash(), 0);
        EXPECT_NE(block.hash_, zeroHash());
    });
}

TEST(Block, HandlesMaxVersionValue) {
    EXPECT_NO_THROW({
        Block block(UINT32_MAX, zeroHash(), 1700000000);
        EXPECT_NE(block.hash_, zeroHash());
    });
}

TEST(Block, HandlesMaxTimestampValue) {
    EXPECT_NO_THROW({
        Block block(1, zeroHash(), UINT64_MAX);
        EXPECT_NE(block.hash_, zeroHash());
    });
}

TEST(Block, AllZeroFieldsStillProduceValidHash) {
    Block block(0, zeroHash(), 0);
    EXPECT_NE(block.hash_, zeroHash());
    EXPECT_EQ(block.hash_.size(), 32u);
}
