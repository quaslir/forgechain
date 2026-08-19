#include "core/ForkResolution.hpp"
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/OrphanPool.hpp"
#include "crypto/CommonTypes.hpp"
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using namespace forgechain::core;
using forgechain::crypto::HashBytes;

namespace {

Block makeChild(const Block &parent, uint64_t timestampSeed) {
    return Block(1, parent.hash_, timestampSeed, {});
}

}  // namespace


TEST(ForkResolution, TipDirectlyOnGenesisIsFound) {
    Blockchain chain;
    Block tip = makeChild(chain.latest(), 1000);
    OrphanPool pool;

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].hash_, tip.hash_);
}

TEST(ForkResolution, TipOnNonGenesisChainTipIsFound) {
    Blockchain chain;
    Block a = makeChild(chain.latest(), 1000);
    chain.add_block(Block(a));
    Block tip = makeChild(a, 2000);
    OrphanPool pool;

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].hash_, tip.hash_);
}

TEST(ForkResolution, TipOnEarlierNonTipBlockIsFound) {
    Blockchain chain;
    Block a = makeChild(chain.latest(), 1000);
    Block aCopy = a;
    chain.add_block(std::move(a));
    Block b = makeChild(chain.latest(), 2000);
    chain.add_block(std::move(b));

    Block tip = makeChild(aCopy, 3000);
    OrphanPool pool;

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].hash_, tip.hash_);
}

TEST(ForkResolution, TwoOrphanChainConnectsToMainChain) {
    Blockchain chain;
    Block orphanA = makeChild(chain.latest(), 1000);
    Block orphanACopy = orphanA;
    OrphanPool pool;
    pool.add_orphan(std::move(orphanA));

    Block tip = makeChild(orphanACopy, 2000);

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    EXPECT_EQ((*result)[0].hash_, orphanACopy.hash_);
    EXPECT_EQ((*result)[1].hash_, tip.hash_);
}

TEST(ForkResolution, ThreeOrphanChainConnectsToMainChain) {
    Blockchain chain;
    Block orphanA = makeChild(chain.latest(), 1000);
    Block orphanACopy = orphanA;
    Block orphanB = makeChild(orphanACopy, 2000);
    Block orphanBCopy = orphanB;

    OrphanPool pool;
    pool.add_orphan(std::move(orphanA));
    pool.add_orphan(std::move(orphanB));

    Block tip = makeChild(orphanBCopy, 3000);

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3u);
    EXPECT_EQ((*result)[0].hash_, orphanACopy.hash_);
    EXPECT_EQ((*result)[1].hash_, orphanBCopy.hash_);
    EXPECT_EQ((*result)[2].hash_, tip.hash_);
}

TEST(ForkResolution, LongOrphanChainWithinDepthLimitConnects) {
    Blockchain chain;
    OrphanPool pool;

    Block current = chain.latest();
    constexpr size_t kChainLength = kMaxForkDepth - 5;
    for (size_t i = 0; i < kChainLength; ++i) {
        Block orphan = makeChild(current, 1000 + i);
        current = orphan;
        pool.add_orphan(std::move(orphan));
    }
    Block tip = makeChild(current, 999999);

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), kChainLength + 1);
    EXPECT_EQ(result->back().hash_, tip.hash_);
    EXPECT_EQ(result->front().prev_hash_, chain.latest().hash_);
}


TEST(ForkResolution, ResultIsOrderedOldestToNewestNotDiscoveryOrder) {
    Blockchain chain;
    Block orphanA = makeChild(chain.latest(), 1000);
    Block orphanACopy = orphanA;
    OrphanPool pool;
    pool.add_orphan(std::move(orphanA));
    Block tip = makeChild(orphanACopy, 2000);

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    EXPECT_EQ((*result)[0].prev_hash_, chain.latest().hash_);
    EXPECT_EQ((*result)[1].prev_hash_, (*result)[0].hash_);
}


TEST(ForkResolution, TipWithCompletelyUnknownParentReturnsNullopt) {
    Blockchain chain;
    OrphanPool pool;
    HashBytes randomParent{};
    randomParent[0] = 0xAB;
    randomParent[5] = 0xCD;
    Block tip(1, randomParent, 1000, {});

    auto result = build_fork_chain(chain, pool, tip);
    EXPECT_FALSE(result.has_value());
}

TEST(ForkResolution, OrphanChainDeadEndsWithoutReachingMainChainReturnsNullopt) {
    Blockchain chain;
    OrphanPool pool;

    HashBytes danglingParent{};
    danglingParent[0] = 0xFF;
    Block orphanA(1, danglingParent, 1000, {});
    Block orphanACopy = orphanA;
    pool.add_orphan(std::move(orphanA));

    Block tip = makeChild(orphanACopy, 2000);

    auto result = build_fork_chain(chain, pool, tip);
    EXPECT_FALSE(result.has_value());
}

TEST(ForkResolution, EmptyPoolAndNoChainMatchReturnsNullopt) {
    Blockchain chain;
    OrphanPool pool;
    HashBytes unknownParent{};
    unknownParent[3] = 0x77;
    Block tip(1, unknownParent, 1000, {});

    auto result = build_fork_chain(chain, pool, tip);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(pool.orphan_count(), 0u);
}

TEST(ForkResolution, ChainExactlyAtDepthLimitIsRejected) {

    Blockchain chain;
    OrphanPool pool;

    HashBytes danglingParent{};
    danglingParent[0] = 0x11;
    Block first(1, danglingParent, 1000, {});
    Block firstCopy = first;
    pool.add_orphan(std::move(first));

    Block current = firstCopy;
    for (size_t i = 1; i < kMaxForkDepth + 10; ++i) {
        Block orphan = makeChild(current, 1000 + i);
        current = orphan;
        pool.add_orphan(std::move(orphan));
    }
    Block tip = makeChild(current, 999999);

    auto result = build_fork_chain(chain, pool, tip);
    EXPECT_FALSE(result.has_value());
}

TEST(ForkResolution, DepthLimitDoesNotFalsePositiveOnShortChain) {
    Blockchain chain;
    OrphanPool pool;
    Block tip = makeChild(chain.latest(), 1000);

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
}


TEST(ForkResolution, DoesNotMutateChainSize) {
    Blockchain chain;
    Block a = makeChild(chain.latest(), 1000);
    chain.add_block(Block(a));
    size_t sizeBefore = chain.size();

    Block tip = makeChild(a, 2000);
    OrphanPool pool;

    [[maybe_unused]] auto result = build_fork_chain(chain, pool, tip);
    EXPECT_EQ(chain.size(), sizeBefore);
}

TEST(ForkResolution, DoesNotMutateOrphanPoolContents) {
    Blockchain chain;
    Block orphanA = makeChild(chain.latest(), 1000);
    Block orphanACopy = orphanA;
    OrphanPool pool;
    pool.add_orphan(std::move(orphanA));
    size_t countBefore = pool.orphan_count();

    Block tip = makeChild(orphanACopy, 2000);
    [[maybe_unused]] auto result = build_fork_chain(chain, pool, tip);

    EXPECT_EQ(pool.orphan_count(), countBefore);
    EXPECT_TRUE(pool.has_orphan(orphanACopy.hash_));
}


TEST(ForkResolution, UnrelatedOrphansInPoolDoNotAffectResult) {
    Blockchain chain;
    OrphanPool pool;

    HashBytes unrelatedParent{};
    unrelatedParent[0] = 0x99;
    Block unrelatedOrphan(1, unrelatedParent, 5000, {});
    pool.add_orphan(std::move(unrelatedOrphan));

    Block realOrphan = makeChild(chain.latest(), 1000);
    Block realOrphanCopy = realOrphan;
    pool.add_orphan(std::move(realOrphan));

    Block tip = makeChild(realOrphanCopy, 2000);

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    EXPECT_EQ((*result)[0].hash_, realOrphanCopy.hash_);
    EXPECT_EQ((*result)[1].hash_, tip.hash_);
}

TEST(ForkResolution, TwoSeparateForksFromSameParentBothResolveIndependently) {
    Blockchain chain;
    Block parent = makeChild(chain.latest(), 1000);
    chain.add_block(Block(parent));

    Block forkX = makeChild(parent, 2000);
    Block forkY = makeChild(parent, 3000);
    OrphanPool pool;

    auto resultX = build_fork_chain(chain, pool, forkX);
    auto resultY = build_fork_chain(chain, pool, forkY);

    ASSERT_TRUE(resultX.has_value());
    ASSERT_TRUE(resultY.has_value());
    ASSERT_EQ(resultX->size(), 1u);
    ASSERT_EQ(resultY->size(), 1u);
    EXPECT_EQ((*resultX)[0].hash_, forkX.hash_);
    EXPECT_EQ((*resultY)[0].hash_, forkY.hash_);
    EXPECT_NE((*resultX)[0].hash_, (*resultY)[0].hash_);
}


TEST(ForkResolution, ReturnedBlocksPreserveFullContent) {
    Blockchain chain;
    Block tip = makeChild(chain.latest(), 12345);
    tip.difficulty_ = 42;
    tip.nonce_ = 777;
    HashBytes originalHash = tip.hash_;
    uint64_t originalTimestamp = tip.timestamp_;
    OrphanPool pool;

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].hash_, originalHash);
    EXPECT_EQ((*result)[0].timestamp_, originalTimestamp);
    EXPECT_EQ((*result)[0].difficulty_, 42u);
    EXPECT_EQ((*result)[0].nonce_, 777u);
}

TEST(ForkResolution, ReturnedChainContentPreservedThroughOrphanBridge) {
    Blockchain chain;
    Block orphanA = makeChild(chain.latest(), 1000);
    orphanA.difficulty_ = 5;
    Block orphanACopy = orphanA;
    OrphanPool pool;
    pool.add_orphan(std::move(orphanA));

    Block tip = makeChild(orphanACopy, 2000);
    tip.difficulty_ = 6;

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);
    EXPECT_EQ((*result)[0].difficulty_, 5u);
    EXPECT_EQ((*result)[1].difficulty_, 6u);
}
