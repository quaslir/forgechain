#include "core/OrphanPool.hpp"
#include "core/Block.hpp"
#include <gtest/gtest.h>
#include <cstddef>
#include <utility>
#include <vector>
#include "crypto/CommonTypes.hpp"
#include <cstdint>
using namespace forgechain::core;
using forgechain::crypto::HashBytes;

namespace {

HashBytes fakeHash(uint8_t seed) {
    HashBytes h{};
    h[0] = seed;
    return h;
}

}  // namespace

TEST(OrphanPool, StartsEmpty) {
    OrphanPool pool;
    EXPECT_EQ(pool.orphan_count(), 0u);
}

TEST(OrphanPool, AddOrphanIncreasesCount) {
    OrphanPool pool;
    Block block(1, fakeHash(0x01), 1700000000, {});
    pool.add_orphan(std::move(block));
    EXPECT_EQ(pool.orphan_count(), 1u);
}

TEST(OrphanPool, HasOrphanIsTrueAfterAdding) {
    OrphanPool pool;
    Block block(1, fakeHash(0x01), 1700000000, {});
    HashBytes hash = block.hash_;
    pool.add_orphan(std::move(block));
    EXPECT_TRUE(pool.has_orphan(hash));
}

TEST(OrphanPool, HasOrphanIsFalseForUnknownHash) {
    OrphanPool pool;
    EXPECT_FALSE(pool.has_orphan(fakeHash(0xFF)));
}

TEST(OrphanPool, FindOrphanReturnsCorrectBlock) {
    OrphanPool pool;
    HashBytes parent = fakeHash(0x02);
    Block block(1, parent, 1700000001, {});
    HashBytes hash = block.hash_;
    pool.add_orphan(std::move(block));

    auto found = pool.find_orphan(hash);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->hash_, hash);
    EXPECT_EQ(found->prev_hash_, parent);
}

TEST(OrphanPool, FindOrphanReturnsNulloptForMissingHash) {
    OrphanPool pool;
    EXPECT_FALSE(pool.find_orphan(fakeHash(0xFF)).has_value());
}

TEST(OrphanPool, FindOrphanOnEmptyPoolReturnsNullopt) {
    OrphanPool pool;
    EXPECT_FALSE(pool.find_orphan(fakeHash(0x00)).has_value());
}

TEST(OrphanPool, FindOrphanByPrevHashLocatesChild) {
    OrphanPool pool;
    HashBytes parent = fakeHash(0x03);
    Block block(1, parent, 1700000002, {});
    HashBytes childHash = block.hash_;
    pool.add_orphan(std::move(block));

    auto found = pool.find_orphan_by_prev_hash(parent);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->hash_, childHash);
}

TEST(OrphanPool, FindOrphanByPrevHashReturnsNulloptWhenNoChildExists) {
    OrphanPool pool;
    Block block(1, fakeHash(0x04), 1700000003, {});
    pool.add_orphan(std::move(block));

    EXPECT_FALSE(pool.find_orphan_by_prev_hash(fakeHash(0xEE)).has_value());
}

TEST(OrphanPool, FindOrphanByPrevHashDistinguishesMultipleOrphans) {
    OrphanPool pool;
    HashBytes parentA = fakeHash(0x05);
    HashBytes parentB = fakeHash(0x06);

    Block childOfA(1, parentA, 1700000004, {});
    HashBytes childOfAHash = childOfA.hash_;
    pool.add_orphan(std::move(childOfA));

    Block childOfB(1, parentB, 1700000005, {});
    HashBytes childOfBHash = childOfB.hash_;
    pool.add_orphan(std::move(childOfB));

    auto foundA = pool.find_orphan_by_prev_hash(parentA);
    ASSERT_TRUE(foundA.has_value());
    EXPECT_EQ(foundA->hash_, childOfAHash);

    auto foundB = pool.find_orphan_by_prev_hash(parentB);
    ASSERT_TRUE(foundB.has_value());
    EXPECT_EQ(foundB->hash_, childOfBHash);
}

TEST(OrphanPool, RemoveOrphanDecreasesCount) {
    OrphanPool pool;
    Block block(1, fakeHash(0x07), 1700000006, {});
    HashBytes hash = block.hash_;
    pool.add_orphan(std::move(block));

    pool.remove_orphan(hash);
    EXPECT_EQ(pool.orphan_count(), 0u);
    EXPECT_FALSE(pool.has_orphan(hash));
}

TEST(OrphanPool, RemoveOrphanOnlyRemovesMatchingEntry) {
    OrphanPool pool;
    Block a(1, fakeHash(0x08), 1700000007, {});
    HashBytes aHash = a.hash_;
    pool.add_orphan(std::move(a));

    Block b(1, fakeHash(0x09), 1700000008, {});
    HashBytes bHash = b.hash_;
    pool.add_orphan(std::move(b));

    pool.remove_orphan(aHash);
    EXPECT_EQ(pool.orphan_count(), 1u);
    EXPECT_FALSE(pool.has_orphan(aHash));
    EXPECT_TRUE(pool.has_orphan(bHash));
}

TEST(OrphanPool, RemovingNonExistentOrphanIsANoOp) {
    OrphanPool pool;
    Block block(1, fakeHash(0x0A), 1700000009, {});
    pool.add_orphan(std::move(block));

    EXPECT_NO_THROW(pool.remove_orphan(fakeHash(0xFF)));
    EXPECT_EQ(pool.orphan_count(), 1u);
}

TEST(OrphanPool, AddingFirstEverOrphanDoesNotThrow) {
    OrphanPool pool;
    Block block(1, fakeHash(0x0B), 1700000010, {});
    EXPECT_NO_THROW(pool.add_orphan(std::move(block)));
}

TEST(OrphanPool, ReAddingSameHashUpdatesRatherThanDuplicates) {
    OrphanPool pool;
    HashBytes parent = fakeHash(0x0C);

    Block first(1, parent, 1700000011, {});
    HashBytes hash = first.hash_;
    pool.add_orphan(std::move(first));
    ASSERT_EQ(pool.orphan_count(), 1u);

    Block duplicate(1, parent, 1700000011, {});
    ASSERT_EQ(duplicate.hash_, hash) << "test setup invariant: expected identical hash";
    pool.add_orphan(std::move(duplicate));

    EXPECT_EQ(pool.orphan_count(), 1u);
}

TEST(OrphanPool, MultipleDistinctOrphansAllRetrievable) {
    OrphanPool pool;
    constexpr int kOrphanCount = 10;
    std::vector<HashBytes> hashes;

    for (int i = 0; i < kOrphanCount; ++i) {
        Block block(1, fakeHash(static_cast<uint8_t>(i)),
                    1700000000 + static_cast<uint64_t>(i), {});
        hashes.push_back(block.hash_);
        pool.add_orphan(std::move(block));
    }

    EXPECT_EQ(pool.orphan_count(), static_cast<size_t>(kOrphanCount));
    for (const auto& h : hashes) {
        EXPECT_TRUE(pool.has_orphan(h));
    }
}

TEST(OrphanPool, OrphanContentIsPreservedThroughAddAndFind) {
    OrphanPool pool;
    HashBytes parent = fakeHash(0x0D);
    Block block(1, parent, 1700000012, {});
    block.difficulty_ = 15;
    block.nonce_ = 999;
    HashBytes hash = block.hash_;

    pool.add_orphan(std::move(block));

    auto found = pool.find_orphan(hash);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->difficulty_, 15u);
    EXPECT_EQ(found->nonce_, 999u);
    EXPECT_EQ(found->prev_hash_, parent);
}
