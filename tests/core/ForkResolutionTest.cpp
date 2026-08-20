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
    ASSERT_EQ(result->blocks.size(), 1u);
    EXPECT_EQ(result->blocks[0].hash_, tip.hash_);
}

TEST(ForkResolution, TipOnNonGenesisChainTipIsFound) {
    Blockchain chain;
    Block a = makeChild(chain.latest(), 1000);
    chain.add_block(Block(a));
    Block tip = makeChild(a, 2000);
    OrphanPool pool;

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->blocks.size(), 1u);
    EXPECT_EQ(result->blocks[0].hash_, tip.hash_);
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
    ASSERT_EQ(result->blocks.size(), 1u);
    EXPECT_EQ(result->blocks[0].hash_, tip.hash_);
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
    ASSERT_EQ(result->blocks.size(), 2u);
    EXPECT_EQ(result->blocks[0].hash_, orphanACopy.hash_);
    EXPECT_EQ(result->blocks[1].hash_, tip.hash_);
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
    ASSERT_EQ(result->blocks.size(), 3u);
    EXPECT_EQ(result->blocks[0].hash_, orphanACopy.hash_);
    EXPECT_EQ(result->blocks[1].hash_, orphanBCopy.hash_);
    EXPECT_EQ(result->blocks[2].hash_, tip.hash_);
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
    EXPECT_EQ(result->blocks.size(), kChainLength + 1);
    EXPECT_EQ(result->blocks.back().hash_, tip.hash_);
    EXPECT_EQ(result->blocks.front().prev_hash_, chain.latest().hash_);
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
    ASSERT_EQ(result->blocks.size(), 2u);
    EXPECT_EQ(result->blocks[0].prev_hash_, chain.latest().hash_);
    EXPECT_EQ(result->blocks[1].prev_hash_, result->blocks[0].hash_);
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
    EXPECT_EQ(result->blocks.size(), 1u);
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
    ASSERT_EQ(result->blocks.size(), 2u);
    EXPECT_EQ(result->blocks[0].hash_, realOrphanCopy.hash_);
    EXPECT_EQ(result->blocks[1].hash_, tip.hash_);
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
    ASSERT_EQ(resultX->blocks.size(), 1u);
    ASSERT_EQ(resultY->blocks.size(), 1u);
    EXPECT_EQ(resultX->blocks[0].hash_, forkX.hash_);
    EXPECT_EQ(resultY->blocks[0].hash_, forkY.hash_);
    EXPECT_NE(resultX->blocks[0].hash_, resultY->blocks[0].hash_);
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
    ASSERT_EQ(result->blocks.size(), 1u);
    EXPECT_EQ(result->blocks[0].hash_, originalHash);
    EXPECT_EQ(result->blocks[0].timestamp_, originalTimestamp);
    EXPECT_EQ(result->blocks[0].difficulty_, 42u);
    EXPECT_EQ(result->blocks[0].nonce_, 777u);
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
    ASSERT_EQ(result->blocks.size(), 2u);
    EXPECT_EQ(result->blocks[0].difficulty_, 5u);
    EXPECT_EQ(result->blocks[1].difficulty_, 6u);
}


TEST(ForkResolution, CommonAncestorIsGenesisWhenForkBranchesDirectlyFromIt) {
    Blockchain chain;
    Block tip = makeChild(chain.latest(), 1000);
    OrphanPool pool;

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->common_ancestor.hash_, chain.latest().hash_);
}

TEST(ForkResolution, CommonAncestorIsTheEarlierBlockNotTheChainTip) {
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
    EXPECT_EQ(result->common_ancestor.hash_, aCopy.hash_);
    EXPECT_NE(result->common_ancestor.hash_, chain.latest().hash_);
}

TEST(ForkResolution, CommonAncestorHasNonTrivialCumulativeWorkFromMainChain) {
    Blockchain chain;
    Block a = makeChild(chain.latest(), 1000);
    a.difficulty_ = 4;
    chain.add_block(Block(a));

    Block tip = makeChild(chain.latest(), 2000);
    OrphanPool pool;

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->common_ancestor.cumulative_work_, chain.latest().cumulative_work_);
    EXPECT_GT(result->common_ancestor.cumulative_work_, 0u);
}

TEST(ForkResolution, CommonAncestorFoundThroughOrphanBridgeMatchesChainBlock) {
    Blockchain chain;
    Block orphanA = makeChild(chain.latest(), 1000);
    Block orphanACopy = orphanA;
    OrphanPool pool;
    pool.add_orphan(std::move(orphanA));
    Block tip = makeChild(orphanACopy, 2000);

    auto result = build_fork_chain(chain, pool, tip);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->common_ancestor.hash_, chain.latest().hash_);
    EXPECT_NE(result->common_ancestor.hash_, orphanACopy.hash_);
}

namespace {

ForkChain makeForkChain(Block ancestor, std::vector<Block> blocks) {
    return ForkChain{.blocks = std::move(blocks), .common_ancestor = std::move(ancestor)};
}

}  // namespace

TEST(ForkResolution, HeavierForkReturnsTrue) {
    Blockchain chain;
    Block a = makeChild(chain.latest(), 1000);
    a.difficulty_ = 4;
    chain.add_block(Block(a));
    Blockchain genesisOnly;
    Block forkTip = makeChild(genesisOnly.latest(), 2000);
    forkTip.difficulty_ = 10;
    ForkChain fork = makeForkChain(genesisOnly.latest(), {forkTip});
    EXPECT_TRUE(is_fork_heavier(chain, fork));
}

TEST(ForkResolution, LighterForkReturnsFalse) {
    Blockchain chain;
    Block a = makeChild(chain.latest(), 1000);
    a.difficulty_ = 10;
    chain.add_block(Block(a));

    Blockchain genesisOnly;
    Block forkTip = makeChild(genesisOnly.latest(), 2000);
    forkTip.difficulty_ = 2;
    ForkChain fork = makeForkChain(genesisOnly.latest(), {forkTip});

    EXPECT_FALSE(is_fork_heavier(chain, fork));
}

TEST(ForkResolution, EqualWeightForkReturnsFalseNotTrue) {
    Blockchain chain;
    Block a = makeChild(chain.latest(), 1000);
    a.difficulty_ = 5;
    chain.add_block(Block(a));

    Blockchain freshChain;
    Block equalTip = makeChild(freshChain.latest(), 3000);
    equalTip.difficulty_ = 5;
    ForkChain tiedFork = makeForkChain(freshChain.latest(), {equalTip});

    ASSERT_EQ(freshChain.latest().cumulative_work_ + equalTip.block_work(),
              chain.latest().cumulative_work_);
    EXPECT_FALSE(is_fork_heavier(chain, tiedFork));
}

TEST(ForkResolution, MultiBlockForkSumsAllBlockWork) {
    Blockchain chain;
    Block a = makeChild(chain.latest(), 1000);
    a.difficulty_ = 8;
    chain.add_block(Block(a));

    Blockchain genesisOnly;
    Block f1 = makeChild(genesisOnly.latest(), 2000);
    f1.difficulty_ = 8;
    Block f2 = makeChild(f1, 2001);
    f2.difficulty_ = 1;
    Block f3 = makeChild(f2, 2002);
    f3.difficulty_ = 1;

    ForkChain fork = makeForkChain(genesisOnly.latest(), {f1, f2, f3});
    EXPECT_TRUE(is_fork_heavier(chain, fork));
}

TEST(ForkResolution, HeavierForkFoundEndToEndThroughBuildForkChain) {
    Blockchain chain;
    HashBytes genesisHash = chain.latest().hash_;
    Block a = makeChild(chain.latest(), 1000);
    a.difficulty_ = 3;
    chain.add_block(Block(a));

    Block orphanTip(1, genesisHash, 2000, {});
    orphanTip.difficulty_ = 15;
    OrphanPool pool;

    auto result = build_fork_chain(chain, pool, orphanTip);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->common_ancestor.hash_, genesisHash);
    EXPECT_TRUE(is_fork_heavier(chain, *result));
}

TEST(ForkResolution, LighterForkFoundEndToEndThroughBuildForkChainIsRejected) {
    Blockchain chain;
    HashBytes genesisHash = chain.latest().hash_;
    Block a = makeChild(chain.latest(), 1000);
    a.difficulty_ = 15;
    chain.add_block(Block(a));

    Block orphanTip(1, genesisHash, 2000, {});
    orphanTip.difficulty_ = 1;
    OrphanPool pool;

    auto result = build_fork_chain(chain, pool, orphanTip);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->common_ancestor.hash_, genesisHash);
    EXPECT_FALSE(is_fork_heavier(chain, *result));
}

TEST(ForkResolution, ZeroDifficultyBlocksContributeMinimalButNonZeroWork) {
    Blockchain chain;

    Block forkTip = makeChild(chain.latest(), 1000);
    forkTip.difficulty_ = 0;
    ForkChain fork = makeForkChain(chain.latest(), {forkTip});

    EXPECT_TRUE(is_fork_heavier(chain, fork));
}
