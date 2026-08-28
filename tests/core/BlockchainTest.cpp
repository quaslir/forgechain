#include "core/Blockchain.hpp"
#include "core/Block.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include "crypto/CommonTypes.hpp"
#include "core/ForkResolution.hpp"
#include <utility>
#include "core/Transaction.hpp"
using namespace forgechain::core;
using forgechain::crypto::HashBytes;

namespace {
HashBytes zeroHash() {
    return HashBytes{};
}
}


TEST(Blockchain, StartsWithExactlyOneBlock) {
    Blockchain chain;
    EXPECT_EQ(chain.size(), 1u);
}

TEST(Blockchain, GenesisBlockHasNonEmptyHash) {
    Blockchain chain;
    EXPECT_NE(chain.latest().hash_, zeroHash());
}

TEST(Blockchain, GenesisIsAccessibleAtHeightZero) {
    Blockchain chain;
    EXPECT_EQ(chain.at(0).hash_, chain.latest().hash_);
}

TEST(Blockchain, TwoFreshBlockchainsHaveIdenticalGenesisBlocks) {
    Blockchain a;
    Blockchain b;
    EXPECT_EQ(a.latest().hash_, b.latest().hash_);
}


TEST(Blockchain, AddBlockIncreasesSize) {
    Blockchain chain;
    Block next(1, chain.latest().hash_, 1700000001, {});
    chain.add_block(std::move(next));
    EXPECT_EQ(chain.size(), 2u);
}

TEST(Blockchain, AddBlockAppendsAtTheEnd) {
    Blockchain chain;
    Block next(1, chain.latest().hash_, 1700000001, {});
    chain.add_block(std::move(next));
    EXPECT_EQ(chain.latest().hash_, next.hash_);
}

TEST(Blockchain, AddBlockDoesNotModifyEarlierBlocks) {
    Blockchain chain;
    auto genesisHashBefore = chain.at(0).hash_;
    Block next(1, chain.latest().hash_, 1700000001, {});
    chain.add_block(std::move(next));
    EXPECT_EQ(chain.at(0).hash_, genesisHashBefore);
}

TEST(Blockchain, MultipleAddBlockCallsPreserveInsertionOrder) {
    Blockchain chain;
    Block b1(1, chain.latest().hash_, 1700000001, {});
    HashBytes b1Hash = b1.hash_;
    chain.add_block(std::move(b1));
    Block b2(1, chain.latest().hash_, 1700000002, {});
    HashBytes b2Hash = b2.hash_;
    chain.add_block(std::move(b2));
    Block b3(1, chain.latest().hash_, 1700000003, {});
    HashBytes b3Hash = b3.hash_;
    chain.add_block(std::move(b3));
    EXPECT_EQ(chain.size(), 4u);
    EXPECT_EQ(chain.at(1).hash_, b1Hash);
    EXPECT_EQ(chain.at(2).hash_, b2Hash);
    EXPECT_EQ(chain.at(3).hash_, b3Hash);
    EXPECT_EQ(chain.latest().hash_, b3Hash);
}

TEST(Blockchain, AddingManyBlocksScalesSizeCorrectly) {
    Blockchain chain;
    constexpr int kBlocksToAdd = 500;
    for (int i = 0; i < kBlocksToAdd; ++i) {
        Block next(1, chain.latest().hash_, static_cast<uint64_t>(1700000000) + static_cast<uint64_t>(i), {});
        chain.add_block(std::move(next));
    }
    EXPECT_EQ(chain.size(), static_cast<size_t>(kBlocksToAdd + 1));
}


TEST(Blockchain, LatestReturnsLastAddedBlock) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1700000001, {});
    chain.add_block(std::move(a));
    Block b(1, chain.latest().hash_, 1700000002, {});
    HashBytes bHash = b.hash_;
    chain.add_block(std::move(b));
    EXPECT_EQ(chain.latest().hash_, bHash);
}

TEST(Blockchain, AtReturnsCorrectBlockForEachHeight) {
    Blockchain chain;
    HashBytes genesisHash = chain.latest().hash_;
    Block a(1, chain.latest().hash_, 1700000001, {});
    HashBytes aHash = a.hash_;
    chain.add_block(std::move(a));
    EXPECT_EQ(chain.at(0).hash_, genesisHash);
    EXPECT_EQ(chain.at(1).hash_, aHash);
}

TEST(Blockchain, SizeMatchesNumberOfBlocksAdded) {
    Blockchain chain;
    EXPECT_EQ(chain.size(), 1u);
    for (int i = 0; i < 10; ++i) {
        Block next(1, chain.latest().hash_, static_cast<uint64_t>(1700000000) + static_cast<uint64_t>(i), {});
        chain.add_block(std::move(next));
        EXPECT_EQ(chain.size(), static_cast<size_t>(i + 2));
    }
}

TEST(Blockchain, AtOutOfBoundsThrows) {
    Blockchain chain;
    EXPECT_THROW((void) chain.at(999), std::out_of_range);
}

TEST(Blockchain, EachBlockReferencesActualPreviousHash) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1700000001, {});
    HashBytes genesisHash = chain.latest().hash_;
    chain.add_block(std::move(a));
    EXPECT_EQ(chain.at(1).prev_hash_, genesisHash);
    Block b(1, chain.latest().hash_, 1700000002, {});
    HashBytes aHash = chain.latest().hash_;
    chain.add_block(std::move(b));
    EXPECT_EQ(chain.at(2).prev_hash_, aHash);
}

TEST(Blockchain, OperatorBracketReturnsSameBlockAsAt) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1700000001, {});
    chain.add_block(std::move(a));

    EXPECT_EQ(chain[0].hash_, chain.at(0).hash_);
    EXPECT_EQ(chain[1].hash_, chain.at(1).hash_);
}

TEST(Blockchain, OperatorBracketReflectsInsertionOrder) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1700000001, {});
    HashBytes aHash = a.hash_;
    chain.add_block(std::move(a));
    Block b(1, chain.latest().hash_, 1700000002, {});
    HashBytes bHash = b.hash_;
    chain.add_block(std::move(b));

    EXPECT_EQ(chain[0].hash_, chain.at(0).hash_);
    EXPECT_EQ(chain[1].hash_, aHash);
    EXPECT_EQ(chain[2].hash_, bHash);
}


TEST(Blockchain, EmptyIsFalseForFreshChain) {
    Blockchain chain;
    EXPECT_FALSE(chain.empty());
}

TEST(Blockchain, EmptyRemainsFalseAfterAddingBlocks) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1700000001, {});
    chain.add_block(std::move(a));
    EXPECT_FALSE(chain.empty());
}

TEST(BlockchainValidation, FreshChainIsValid) {
    Blockchain chain;
    EXPECT_TRUE(chain.is_valid());
}

TEST(BlockchainValidation, HonestlyBuiltChainIsValid) {
    Blockchain chain;
    for (int i = 0; i < 20; ++i) {
        Block next(1, chain.latest().hash_, static_cast<uint64_t>(1700000000) + static_cast<uint64_t>(i), {});
        chain.add_block(std::move(next));
    }
    EXPECT_TRUE(chain.is_valid());
}

TEST(BlockchainValidation, DetectsBrokenLinkage) {
    Blockchain chain;

    HashBytes wrongPrevHash{};
    wrongPrevHash[0] = 0xFF;

    Block brokenLink(1, wrongPrevHash, 1700000001, {});
    chain.add_block(std::move(brokenLink));

    EXPECT_FALSE(chain.is_valid());
}

TEST(BlockchainValidation, DetectsTamperedFieldWithStaleHash) {
    Blockchain chain;

    Block tampered(1, chain.latest().hash_, 1700000001, {});
    tampered.timestamp_ = 9999999999ULL;

    chain.add_block(std::move(tampered));

    EXPECT_FALSE(chain.is_valid());
}

TEST(BlockchainValidation, DetectsTamperingInAMiddleBlockNotJustTheTip) {
    Blockchain chain;

    Block first(1, chain.latest().hash_, 1700000001, {});
    chain.add_block(std::move(first));

    Block corruptedMiddle(1, chain.latest().hash_, 1700000002, {});
    corruptedMiddle.difficulty_ = 42;
    chain.add_block(std::move(corruptedMiddle));

    Block last(1, chain.latest().hash_, 1700000003, {});
    chain.add_block(std::move(last));

    EXPECT_FALSE(chain.is_valid());
}

TEST(BlockchainValidation, DetectsTamperedNonceField) {
    Blockchain chain;
    Block tampered(1, chain.latest().hash_, 1700000001, {});
    tampered.nonce_ = 12345;
    chain.add_block(std::move(tampered));

    EXPECT_FALSE(chain.is_valid());
}

TEST(BlockchainValidation, DetectsTamperedVersionField) {
    Blockchain chain;
    Block tampered(1, chain.latest().hash_, 1700000001, {});
    tampered.version_ = 999;
    chain.add_block(std::move(tampered));

    EXPECT_FALSE(chain.is_valid());
}

TEST(BlockchainValidation, ValidAfterManyBlocksWithNoTampering) {
    Blockchain chain;
    constexpr int kBlocksToAdd = 200;
    for (int i = 0; i < kBlocksToAdd; ++i) {
        Block next(1, chain.latest().hash_, static_cast<uint64_t>(1700000000) + static_cast<uint64_t>(i), {});
        chain.add_block(std::move(next));
    }
    EXPECT_TRUE(chain.is_valid());
}

TEST(BlockchainValidation, DetectsReorderedBlocks) {
    Blockchain chain;

    Block first(1, chain.latest().hash_, 1700000001, {});
    Block second(1, first.hash_, 1700000002, {});

    chain.add_block(std::move(second));
    chain.add_block(std::move(first));

    EXPECT_FALSE(chain.is_valid());
}

TEST(BlockchainValidation, InvalidStaysInvalidEvenWithMoreHonestBlocksAfter) {
    Blockchain chain;

    Block tampered(1, chain.latest().hash_, 1700000001, {});
    tampered.version_ = 999;
    chain.add_block(std::move(tampered));

    for (int i = 0; i < 10; ++i) {
        Block next(1, chain.latest().hash_, static_cast<uint64_t>(1700000010) + static_cast<uint64_t>(i), {});
        chain.add_block(std::move(next));
    }

    EXPECT_FALSE(chain.is_valid());
}

TEST(Blockchain, FindHeightLocatesGenesis) {
    Blockchain chain;
    auto height = chain.find_height(chain.latest().hash_);
    ASSERT_TRUE(height.has_value());
    EXPECT_EQ(*height, 0u);
}

TEST(Blockchain, FindHeightLocatesNonGenesisBlock) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1000, {});
    HashBytes aHash = a.hash_;
    chain.add_block(std::move(a));
    Block b(1, chain.latest().hash_, 2000, {});
    chain.add_block(std::move(b));

    auto height = chain.find_height(aHash);
    ASSERT_TRUE(height.has_value());
    EXPECT_EQ(*height, 1u);
}

TEST(Blockchain, FindHeightReturnsNulloptForUnknownHash) {
    Blockchain chain;
    HashBytes unknown{};
    unknown[0] = 0xFF;
    EXPECT_FALSE(chain.find_height(unknown).has_value());
}

TEST(Blockchain, ReorganizeToSucceedsAndReplacesTip) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1000, {});
    chain.add_block(std::move(a));
    Block b(1, chain.latest().hash_, 2000, {});
    chain.add_block(std::move(b));

    HashBytes genesisHash = chain.at(0).hash_;
    Block fork1(1, genesisHash, 5000, {});
    fork1.difficulty_ = 10;
    fork1.hash_ = fork1.compute_hash();
    HashBytes fork1Hash = fork1.hash_;
    Block ancestor = chain.at(0);

    ForkChain fc{.blocks = {fork1}, .common_ancestor = ancestor};
    auto discarded = chain.reorganize_to(std::move(fc));

    ASSERT_TRUE(discarded.has_value());
    EXPECT_EQ(chain.size(), 2u);
    EXPECT_EQ(chain.latest().hash_, fork1Hash);
    EXPECT_EQ(chain.at(0).hash_, genesisHash);
}

TEST(Blockchain, ReorganizeToReturnsDiscardedBlocksInOriginalOrder) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1000, {});
    HashBytes aHash = a.hash_;
    chain.add_block(std::move(a));
    Block b(1, chain.latest().hash_, 2000, {});
    HashBytes bHash = b.hash_;
    chain.add_block(std::move(b));

    Block ancestor = chain.at(0);
    HashBytes genesisHash = ancestor.hash_;
    Block fork1(1, genesisHash, 5000, {});
    fork1.difficulty_ = 10;
    fork1.hash_ = fork1.compute_hash();

    ForkChain fc{.blocks = {fork1}, .common_ancestor = ancestor};
    auto discarded = chain.reorganize_to(std::move(fc));

    ASSERT_TRUE(discarded.has_value());
    ASSERT_EQ(discarded->size(), 2u);
    EXPECT_EQ((*discarded)[0].hash_, aHash);
    EXPECT_EQ((*discarded)[1].hash_, bHash);
}

TEST(Blockchain, ReorganizeToReturnsNulloptWhenAncestorNotFound) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1000, {});
    chain.add_block(std::move(a));

    HashBytes unknownAncestorHash{};
    unknownAncestorHash[0] = 0xEE;
    Block fakeAncestor(1, HashBytes{}, 0, {});
    fakeAncestor.hash_ = unknownAncestorHash;

    Block fork1(1, unknownAncestorHash, 5000, {});
    fork1.difficulty_ = 10;
    fork1.hash_ = fork1.compute_hash();

    size_t sizeBefore = chain.size();
    ForkChain fc{.blocks = {fork1}, .common_ancestor = fakeAncestor};
    auto discarded = chain.reorganize_to(std::move(fc));

    EXPECT_FALSE(discarded.has_value());
    EXPECT_EQ(chain.size(), sizeBefore);
}

TEST(Blockchain, ReorganizeToRecomputesCumulativeWorkForAppliedBlocks) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1000, {});
    a.difficulty_ = 2;
    a.hash_ = a.compute_hash();
    chain.add_block(std::move(a));

    Block ancestor = chain.at(0);
    Block fork1(1, ancestor.hash_, 5000, {});
    fork1.difficulty_ = 4;
    fork1.hash_ = fork1.compute_hash();
    Block fork2(1, fork1.hash_, 5001, {});
    fork2.difficulty_ = 4;
    fork2.hash_ = fork2.compute_hash();

    ForkChain fc{.blocks = {fork1, fork2}, .common_ancestor = ancestor};
    auto discarded = chain.reorganize_to(std::move(fc));

    ASSERT_TRUE(discarded.has_value());
    EXPECT_EQ(chain.latest().cumulative_work_, fork1.block_work() + fork2.block_work());
}

TEST(Blockchain, ReorganizeToWithNoDiscardedBlocksReturnsEmptyVector) {
    Blockchain chain;
    Block ancestor = chain.at(0);
    Block fork1(1, ancestor.hash_, 5000, {});
    fork1.difficulty_ = 3;
    fork1.hash_ = fork1.compute_hash();

    ForkChain fc{.blocks = {fork1}, .common_ancestor = ancestor};
    auto discarded = chain.reorganize_to(std::move(fc));

    ASSERT_TRUE(discarded.has_value());
    EXPECT_TRUE(discarded->empty());
    EXPECT_EQ(chain.size(), 2u);
}

TEST(Blockchain, ClassifyNewBlockAcceptsCoinbaseAsFirstTransaction) {
    Blockchain chain;
    Transaction coinbase(kCoinbaseSender, "alice", 50, {}, 0);
    Block block(1, chain.latest().hash_, 5000, {coinbase});
    block.difficulty_ = 3;
    block.hash_ = block.compute_hash();

    EXPECT_EQ(chain.classify_new_block(block), BlockValidation::Valid);
}

TEST(Blockchain, ClassifyNewBlockAcceptsCoinbaseFollowedByRegularTransactions) {
    Blockchain chain;
    Transaction coinbase(kCoinbaseSender, "alice", 50, {}, 0);
    Transaction regular("alice", "bob", 10, {}, 0);
    Block block(1, chain.latest().hash_, 5000, {coinbase, regular});
    block.difficulty_ = 3;
    block.hash_ = block.compute_hash();

    EXPECT_EQ(chain.classify_new_block(block), BlockValidation::Valid);
}

TEST(Blockchain, ClassifyNewBlockRejectsCoinbaseAsSecondTransaction) {
    Blockchain chain;
    Transaction regular("alice", "bob", 10, {}, 0);
    Transaction coinbase(kCoinbaseSender, "mallory", 50, {}, 0);
    Block block(1, chain.latest().hash_, 5000, {regular, coinbase});
    block.difficulty_ = 3;
    block.hash_ = block.compute_hash();

    EXPECT_EQ(chain.classify_new_block(block), BlockValidation::Invalid);
}

TEST(Blockchain, ClassifyNewBlockRejectsTwoCoinbaseTransactions) {
    Blockchain chain;
    Transaction coinbase1(kCoinbaseSender, "alice", 50, {}, 0);
    Transaction coinbase2(kCoinbaseSender, "mallory", 50, {}, 0);
    Block block(1, chain.latest().hash_, 5000, {coinbase1, coinbase2});
    block.difficulty_ = 3;
    block.hash_ = block.compute_hash();

    EXPECT_EQ(chain.classify_new_block(block), BlockValidation::Invalid);
}

TEST(Blockchain, ClassifyNewBlockWithNoCoinbaseIsUnaffectedByCoinbaseCheck) {
    Blockchain chain;
    Transaction regular1("alice", "bob", 10, {}, 0);
    Transaction regular2("bob", "charlie", 5, {}, 0);
    Block block(1, chain.latest().hash_, 5000, {regular1, regular2});
    block.difficulty_ = 3;
    block.hash_ = block.compute_hash();

    EXPECT_EQ(chain.classify_new_block(block), BlockValidation::Valid);
}

TEST(Blockchain, ClassifyNewBlockRejectsMisplacedCoinbaseEvenWhenAlsoAFork) {
    Blockchain chain;
    Transaction regular("alice", "bob", 10, {}, 0);
    Transaction coinbase(kCoinbaseSender, "mallory", 50, {}, 0);
    Block block(1, zeroHash(), 5000, {regular, coinbase});
    block.difficulty_ = 3;
    block.hash_ = block.compute_hash();

    EXPECT_EQ(chain.classify_new_block(block), BlockValidation::Invalid);
}
