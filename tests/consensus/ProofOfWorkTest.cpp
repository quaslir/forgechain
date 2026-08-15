#include "consensus/ProofOfWork.hpp"

#include <gtest/gtest.h>
#include "crypto/Hash.hpp"
#include <cstdint>
#include <cstddef>
#include "crypto/CommonTypes.hpp"
using namespace forgechain::consensus;
using namespace forgechain::core;
using forgechain::crypto::HashBytes;

namespace {

HashBytes zeroHash() {
    return HashBytes{};
}

HashBytes allOnesHash() {
    HashBytes h;
    h.fill(0xFF);
    return h;
}

HashBytes hashWithLeadingZeroBits(int leadingZeroBits) {
    HashBytes h{};
    int fullZeroBytes = leadingZeroBits / 8;
    int remainderBits = leadingZeroBits % 8;

    if (static_cast<size_t>(fullZeroBytes) < h.size()) {
        if (remainderBits > 0) {
            h[static_cast<size_t>(fullZeroBytes)] =
                static_cast<uint8_t>(0x80 >> remainderBits);
        } else if (static_cast<size_t>(fullZeroBytes) < h.size()) {
            h[static_cast<size_t>(fullZeroBytes)] = 0x80;
        }

        for (size_t i = static_cast<size_t>(fullZeroBytes) + 1; i < h.size(); ++i) {
            h[i] = 0xFF;
        }
    }
    return h;
}

}

TEST(MeetsTarget, ZeroDifficultyAlwaysPasses) {
    EXPECT_TRUE(meets_target(allOnesHash(), 0));
}

TEST(MeetsTarget, AllZeroHashSatisfiesAnyReasonableDifficulty) {
    EXPECT_TRUE(meets_target(zeroHash(), 256));
}

TEST(MeetsTarget, AllOnesHashFailsAnyPositiveDifficulty) {
    EXPECT_FALSE(meets_target(allOnesHash(), 1));
}

TEST(MeetsTarget, ExactBoundaryPasses) {
    auto hash = hashWithLeadingZeroBits(12);
    EXPECT_TRUE(meets_target(hash, 12));
}

TEST(MeetsTarget, OneBitShortOfTargetFails) {
    auto hash = hashWithLeadingZeroBits(12);
    EXPECT_FALSE(meets_target(hash, 13));
}

TEST(MeetsTarget, HashExceedingRequiredZerosStillPasses) {
    auto hash = hashWithLeadingZeroBits(20);
    EXPECT_TRUE(meets_target(hash, 12));
}

TEST(MeetsTarget, DoesNotOverCountPastFirstOneBit) {
    HashBytes h{};
    h[0] = 0x0F;
    h[1] = 0x00;

    EXPECT_TRUE(meets_target(h, 4));
    EXPECT_FALSE(meets_target(h, 5));
    EXPECT_FALSE(meets_target(h, 12));
}

TEST(MeetsTarget, SingleByteBoundaryCases) {
    HashBytes h{};
    h[0] = 0x00;
    h[1] = 0xFF;

    EXPECT_TRUE(meets_target(h, 8));
    EXPECT_FALSE(meets_target(h, 9));
}

TEST(MeetsTarget, IsDeterministic) {
    auto hash = hashWithLeadingZeroBits(16);
    EXPECT_EQ(meets_target(hash, 16), meets_target(hash, 16));
    EXPECT_EQ(meets_target(hash, 10), meets_target(hash, 10));
}

TEST(MeetsTarget, DoesNotMutateInput) {
    auto hash = hashWithLeadingZeroBits(10);
    auto copy = hash;
    meets_target(hash, 10);
    EXPECT_EQ(hash, copy);
}

TEST(MineBlock, ProducesBlockSatisfyingItsOwnDifficulty) {
    constexpr uint32_t kDifficulty = 8;
    Block block = mine_block(1, HashBytes{}, 1700000000, kDifficulty, {});

    EXPECT_TRUE(meets_target(block.hash_, kDifficulty));
}

TEST(MineBlock, MinedHashMatchesRecomputationFromCurrentFields) {
    constexpr uint32_t kDifficulty = 8;
    Block block = mine_block(1, HashBytes{}, 1700000000, kDifficulty, {});

    auto recomputed = forgechain::crypto::double_sha_256(block.serialize());
    EXPECT_EQ(block.hash_, recomputed);
}

TEST(MineBlock, StoresRequestedDifficulty) {
    constexpr uint32_t kDifficulty = 8;
    Block block = mine_block(1, HashBytes{}, 1700000000, kDifficulty, {});

    EXPECT_EQ(block.difficulty_, kDifficulty);
}

TEST(MineBlock, StoresGivenPrevHashUnchanged) {
    HashBytes prevHash{};
    prevHash[0] = 0xAB;

    Block block = mine_block(1, prevHash, 1700000000, 8, {});

    EXPECT_EQ(block.prev_hash_, prevHash);
}

TEST(MineBlock, ZeroDifficultySucceedsImmediatelyWithNonceZero) {
    Block block = mine_block(1, HashBytes{}, 1700000000, 0, {});
    EXPECT_EQ(block.nonce_, 0u);
}

TEST(MineBlock, HigherDifficultyBlockStillPassesLowerDifficultyCheck) {
    constexpr uint32_t kDifficulty = 12;
    Block block = mine_block(1, HashBytes{}, 1700000000, kDifficulty, {});

    EXPECT_TRUE(meets_target(block.hash_, kDifficulty));
    EXPECT_TRUE(meets_target(block.hash_, kDifficulty - 4));
    EXPECT_TRUE(meets_target(block.hash_, 0));
}

TEST(MineBlock, DifferentPrevHashProducesDifferentMinedBlock) {
    HashBytes prevA{};
    prevA[0] = 0x01;
    HashBytes prevB{};
    prevB[0] = 0x02;

    Block blockA = mine_block(1, prevA, 1700000000, 8, {});
    Block blockB = mine_block(1, prevB, 1700000000, 8, {});

    EXPECT_NE(blockA.hash_, blockB.hash_);
}

TEST(MineBlock, SucceedsAcrossSeveralSmallDifficulties) {
    for (uint32_t difficulty : {0u, 1u, 4u, 8u, 10u}) {
        Block block = mine_block(1, HashBytes{}, 1700000000, difficulty, {});
        EXPECT_TRUE(meets_target(block.hash_, difficulty))
            << "failed at difficulty " << difficulty;
    }
}
