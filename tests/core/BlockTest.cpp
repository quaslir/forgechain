#include "core/Block.hpp"
#include "crypto/Hash.hpp"
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "crypto/CommonTypes.hpp"
#include <vector>
#include "core/Transaction.hpp"
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
    Block block(1, prevHash, 1700000000, {});

    EXPECT_EQ(block.version_, 1u);
    EXPECT_EQ(block.prev_hash_, prevHash);
    EXPECT_EQ(block.timestamp_, 1700000000u);
}

TEST(Block, ConstructorDefaultsNonceToZero) {
    Block block(1, zeroHash(), 1700000000, {});
    EXPECT_EQ(block.nonce_, 0u);
}

TEST(Block, ConstructorComputesNonEmptyHash) {
    Block block(1, zeroHash(), 1700000000, {});
    EXPECT_NE(block.hash_, zeroHash());
}


TEST(Block, SerializeIsDeterministic) {
    HashBytes prevHash = fakeHash(0x11);
    Block a(1, prevHash, 1700000000, {});
    Block b(1, prevHash, 1700000000, {});

    EXPECT_EQ(a.serialize(), b.serialize());
}

TEST(Block, SerializeIsNonEmpty) {
    Block block(1, zeroHash(), 1700000000, {});
    EXPECT_FALSE(block.serialize().empty());
}

TEST(Block, SerializeChangesWhenVersionChanges) {
    Block a(1, zeroHash(), 1700000000, {});
    Block b(2, zeroHash(), 1700000000, {});
    EXPECT_NE(a.serialize(), b.serialize());
}

TEST(Block, SerializeChangesWhenPrevHashChanges) {
    Block a(1, fakeHash(0x01), 1700000000, {});
    Block b(1, fakeHash(0x02), 1700000000, {});
    EXPECT_NE(a.serialize(), b.serialize());
}

TEST(Block, SerializeChangesWhenTimestampChanges) {
    Block a(1, zeroHash(), 1700000000, {});
    Block b(1, zeroHash(), 1700000001, {});
    EXPECT_NE(a.serialize(), b.serialize());
}


TEST(Block, IdenticalFieldsProduceIdenticalHash) {
    HashBytes prevHash = fakeHash(0x42);
    Block a(1, prevHash, 1700000000, {});
    Block b(1, prevHash, 1700000000, {});

    EXPECT_EQ(a.hash_, b.hash_);
}

TEST(Block, HashIsDeterministicAcrossManyConstructions) {
    HashBytes prevHash = fakeHash(0x99);
    Block first(1, prevHash, 1700000000, {});

    for (int i = 0; i < 1000; ++i) {
        Block again(1, prevHash, 1700000000, {});
        EXPECT_EQ(again.hash_, first.hash_) << "mismatch at iteration " << i;
    }
}

TEST(Block, DifferentVersionProducesDifferentHash) {
    Block a(1, zeroHash(), 1700000000, {});
    Block b(2, zeroHash(), 1700000000, {});
    EXPECT_NE(a.hash_, b.hash_);
}

TEST(Block, DifferentPrevHashProducesDifferentHash) {
    Block a(1, fakeHash(0x01), 1700000000, {});
    Block b(1, fakeHash(0x02), 1700000000, {});
    EXPECT_NE(a.hash_, b.hash_);
}

TEST(Block, DifferentTimestampProducesDifferentHash) {
    Block a(1, zeroHash(), 1700000000, {});
    Block b(1, zeroHash(), 1700000001, {});
    EXPECT_NE(a.hash_, b.hash_);
}

TEST(Block, HashMatchesRecomputationFromSerialize) {
    Block block(1, fakeHash(0x77), 1700000000, {});

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
                    static_cast<uint64_t>(1700000000) + static_cast<uint64_t>(i), {});

        seenHashes.insert(forgechain::crypto::to_hex(block.hash_));
    }

    EXPECT_EQ(seenHashes.size(), static_cast<size_t>(kIterations));
}


TEST(Block, HandlesZeroTimestamp) {
    EXPECT_NO_THROW({
        Block block(1, zeroHash(), 0, {});
        EXPECT_NE(block.hash_, zeroHash());
    });
}

TEST(Block, HandlesMaxVersionValue) {
    EXPECT_NO_THROW({
        Block block(UINT32_MAX, zeroHash(), 1700000000, {});
        EXPECT_NE(block.hash_, zeroHash());
    });
}

TEST(Block, HandlesMaxTimestampValue) {
    EXPECT_NO_THROW({
        Block block(1, zeroHash(), UINT64_MAX, {});
        EXPECT_NE(block.hash_, zeroHash());
    });
}

TEST(Block, AllZeroFieldsStillProduceValidHash) {
    Block block(0, zeroHash(), 0, {});
    EXPECT_NE(block.hash_, zeroHash());
    EXPECT_EQ(block.hash_.size(), 32u);
}



TEST(BlockDeserialize, RoundTripPreservesAllFieldsWithNoTransactions) {
    Block original(3, fakeHash(0x55), 1700000123, {});
    original.difficulty_ = 42;
    original.nonce_ = 987654;

    original.hash_ = original.compute_hash();

    auto restored = Block::deserialize(original.serialize());
    ASSERT_TRUE(restored.has_value());

    EXPECT_EQ(restored->version_, original.version_);
    EXPECT_EQ(restored->prev_hash_, original.prev_hash_);
    EXPECT_EQ(restored->merkle_root_, original.merkle_root_);
    EXPECT_EQ(restored->timestamp_, original.timestamp_);
    EXPECT_EQ(restored->difficulty_, original.difficulty_);
    EXPECT_EQ(restored->nonce_, original.nonce_);
    EXPECT_TRUE(restored->transactions_.empty());
    EXPECT_EQ(restored->hash_, original.hash_);
}

TEST(BlockDeserialize, RoundTripPreservesTransactions) {
    std::vector<Transaction> txs;
    txs.emplace_back("alice", "bob", 100, forgechain::crypto::bytes{}, 0);
    txs.emplace_back("bob", "carol", 50, forgechain::crypto::bytes{}, 0);
    txs.emplace_back("carol", "alice", 25, forgechain::crypto::bytes{}, 0);

    Block original(1, fakeHash(0x22), 1700000456, txs);
    original.difficulty_ = 7;
    original.nonce_ = 13;
    original.hash_ = original.compute_hash();

    auto restored = Block::deserialize(original.serialize());
    ASSERT_TRUE(restored.has_value());

    ASSERT_EQ(restored->transactions_.size(), 3u);
    EXPECT_EQ(restored->transactions_[0].sender_, "alice");
    EXPECT_EQ(restored->transactions_[0].recipient_, "bob");
    EXPECT_EQ(restored->transactions_[0].amount_, 100u);
    EXPECT_EQ(restored->transactions_[1].sender_, "bob");
    EXPECT_EQ(restored->transactions_[1].recipient_, "carol");
    EXPECT_EQ(restored->transactions_[1].amount_, 50u);
    EXPECT_EQ(restored->transactions_[2].sender_, "carol");
    EXPECT_EQ(restored->transactions_[2].recipient_, "alice");
    EXPECT_EQ(restored->transactions_[2].amount_, 25u);

    EXPECT_EQ(restored->hash_, original.hash_);
}

TEST(BlockDeserialize, RoundTripPreservesHashExactly) {
    std::vector<Transaction> txs;
    txs.emplace_back("miner", "reward_pool", 5000000, forgechain::crypto::bytes{}, 0);

    Block original(2, fakeHash(0x9A), 1700009999, txs);
    original.difficulty_ = 123456;
    original.nonce_ = 789;
    original.hash_ = original.compute_hash();

    auto serialized = original.serialize();
    auto restored = Block::deserialize(serialized);

    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->hash_, original.hash_);
    EXPECT_EQ(restored->serialize(), serialized);
}

TEST(BlockDeserialize, EmptyPayloadIsRejected) {
    forgechain::crypto::bytes payload;
    EXPECT_FALSE(Block::deserialize(payload).has_value());
}

TEST(BlockDeserialize, TruncatedBeforeVersionIsRejected) {
    forgechain::crypto::bytes payload{0x01, 0x02};
    EXPECT_FALSE(Block::deserialize(payload).has_value());
}

TEST(BlockDeserialize, TruncatedMidHashesIsRejected) {
    Block block(1, fakeHash(0x33), 1700000000, {});
    auto full = block.serialize();
    forgechain::crypto::bytes truncated(full.begin(), full.begin() + 10);
    EXPECT_FALSE(Block::deserialize(truncated).has_value());
}

TEST(BlockDeserialize, TruncatedMidTransactionListIsRejected) {
    std::vector<Transaction> txs;
    txs.emplace_back("alice", "bob", 100, forgechain::crypto::bytes{}, 0);
    txs.emplace_back("bob", "carol", 50, forgechain::crypto::bytes{}, 0);

    Block block(1, fakeHash(0x44), 1700000000, txs);
    auto full = block.serialize();
    forgechain::crypto::bytes truncated(full.begin(), full.end() - 5);
    EXPECT_FALSE(Block::deserialize(truncated).has_value());
}

TEST(BlockDeserialize, DeclaredTxCountHigherThanActualDataIsRejected) {
    Block block(1, fakeHash(0x66), 1700000000, {});
    auto payload = block.serialize();

    constexpr size_t kTxCountOffset = 4 + 32 + 32 + 8 + 4 + 4;
    ASSERT_LE(kTxCountOffset + sizeof(uint32_t), payload.size());

    uint32_t fake_count = 999;
    std::memcpy(payload.data() + kTxCountOffset, &fake_count, sizeof(fake_count));

    EXPECT_FALSE(Block::deserialize(payload).has_value());
}

TEST(BlockDeserialize, TrailingGarbageAfterValidBlockIsRejected) {
    Block block(1, fakeHash(0x77), 1700000000, {});
    auto payload = block.serialize();
    payload.push_back(0xFF);

    EXPECT_FALSE(Block::deserialize(payload).has_value());
}

TEST(BlockDeserialize, MalformedTransactionInsideBlockIsRejected) {
    Block block(1, fakeHash(0x88), 1700000000, {});
    auto payload = block.serialize();
    uint32_t fake_tx_len = 4;
    forgechain::crypto::bytes fake_tx_bytes{0x01, 0x02, 0x03, 0x04};

    constexpr size_t kTxCountOffset = 4 + 32 + 32 + 8 + 4 + 4;
    uint32_t new_count = 1;
    std::memcpy(payload.data() + kTxCountOffset, &new_count, sizeof(new_count));

    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&fake_tx_len),
                    reinterpret_cast<const uint8_t*>(&fake_tx_len) + sizeof(fake_tx_len));
    payload.insert(payload.end(), fake_tx_bytes.begin(), fake_tx_bytes.end());

    EXPECT_FALSE(Block::deserialize(payload).has_value());
}
