#include "core/Blockchain.hpp"
#include "core/Block.hpp"
#include "crypto/Hash.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
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
    Block next(1, chain.latest().hash_, 1700000001);

    chain.add_block(next);

    EXPECT_EQ(chain.size(), 2u);
}

TEST(Blockchain, AddBlockAppendsAtTheEnd) {
    Blockchain chain;
    Block next(1, chain.latest().hash_, 1700000001);

    chain.add_block(next);

    EXPECT_EQ(chain.latest().hash_, next.hash_);
}

TEST(Blockchain, AddBlockDoesNotModifyEarlierBlocks) {
    Blockchain chain;
    auto genesisHashBefore = chain.at(0).hash_;

    Block next(1, chain.latest().hash_, 1700000001);
    chain.add_block(next);

    EXPECT_EQ(chain.at(0).hash_, genesisHashBefore);
}

TEST(Blockchain, MultipleAddBlockCallsPreserveInsertionOrder) {
    Blockchain chain;

    Block b1(1, chain.latest().hash_, 1700000001);
    chain.add_block(b1);

    Block b2(1, chain.latest().hash_, 1700000002);
    chain.add_block(b2);

    Block b3(1, chain.latest().hash_, 1700000003);
    chain.add_block(b3);

    EXPECT_EQ(chain.size(), 4u);
    EXPECT_EQ(chain.at(1).hash_, b1.hash_);
    EXPECT_EQ(chain.at(2).hash_, b2.hash_);
    EXPECT_EQ(chain.at(3).hash_, b3.hash_);
    EXPECT_EQ(chain.latest().hash_, b3.hash_);
}

TEST(Blockchain, AddingManyBlocksScalesSizeCorrectly) {
    Blockchain chain;

    constexpr int kBlocksToAdd = 500;
    for (int i = 0; i < kBlocksToAdd; ++i) {
        Block next(1, chain.latest().hash_, 1700000000ULL + static_cast<uint64_t>(i));
        chain.add_block(next);
    }

    EXPECT_EQ(chain.size(), static_cast<size_t>(kBlocksToAdd + 1));
}


TEST(Blockchain, LatestReturnsLastAddedBlock) {
    Blockchain chain;
    Block a(1, chain.latest().hash_, 1700000001);
    chain.add_block(a);
    Block b(1, chain.latest().hash_, 1700000002);
    chain.add_block(b);

    EXPECT_EQ(chain.latest().hash_, b.hash_);
}

TEST(Blockchain, AtReturnsCorrectBlockForEachHeight) {
    Blockchain chain;
    HashBytes genesisHash = chain.latest().hash_;

    Block a(1, chain.latest().hash_, 1700000001);
    chain.add_block(a);

    EXPECT_EQ(chain.at(0).hash_, genesisHash);
    EXPECT_EQ(chain.at(1).hash_, a.hash_);
}

TEST(Blockchain, SizeMatchesNumberOfBlocksAdded) {
    Blockchain chain;
    EXPECT_EQ(chain.size(), 1u);

    for (int i = 0; i < 10; ++i) {
        Block next(1, chain.latest().hash_, 1700000000ULL + static_cast<uint64_t>(i));
        chain.add_block(next);
        EXPECT_EQ(chain.size(), static_cast<size_t>(i + 2));
    }
}

TEST(Blockchain, AtOutOfBoundsThrows) {
    Blockchain chain;
    EXPECT_THROW((void) chain.at(999), std::out_of_range);
}


TEST(Blockchain, EachBlockReferencesActualPreviousHash) {
    Blockchain chain;

    Block a(1, chain.latest().hash_, 1700000001);
    HashBytes genesisHash = chain.latest().hash_;
    chain.add_block(a);

    EXPECT_EQ(chain.at(1).prev_hash_, genesisHash);

    Block b(1, chain.latest().hash_, 1700000002);
    HashBytes aHash = chain.latest().hash_;
    chain.add_block(b);

    EXPECT_EQ(chain.at(2).prev_hash_, aHash);
}
