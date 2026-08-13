#include "consensus/ProofOfWork.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>

using namespace forgechain::consensus;



TEST(Retarget, IncreasesWhenBlocksCameMuchFasterThanExpected) {
    EXPECT_EQ(retarget(10, 1, 10), 11u);
}

TEST(Retarget, DecreasesWhenBlocksCameMuchSlowerThanExpected) {
    EXPECT_EQ(retarget(10, 25, 10), 9u);
}

TEST(Retarget, UnchangedWhenWithinNeutralZone) {
    EXPECT_EQ(retarget(10, 8, 10), 10u);
}



TEST(Retarget, ExactlyAtLowerBoundaryDoesNotIncrease) {
    EXPECT_EQ(retarget(10, 5,10), 10u);
}

TEST(Retarget, OneBelowLowerBoundaryIncreases) {
    EXPECT_EQ(retarget(10, 4, 10), 11u);
}

TEST(Retarget, ExactlyAtUpperBoundaryDoesNotDecrease) {
    EXPECT_EQ(retarget(10, 20, 10), 10u);
}

TEST(Retarget, OneAboveUpperBoundaryDecreases) {
    EXPECT_EQ(retarget(10, 21, 10), 9u);
}

TEST(Retarget, OddExpectedTimeBoundaryUsesIntegerDivision) {
    EXPECT_EQ(retarget(10, 2, 5), 10u);
    EXPECT_EQ(retarget(10, 1, 5), 11u);
}




TEST(Retarget, StaysAtZeroWhenAlreadyZeroAndBlocksAreSlow) {
    EXPECT_EQ(retarget(0,1000000, 10), 0u);
}

TEST(Retarget, DropsFromOneToZeroExactlyOnceNotBelow) {
    EXPECT_EQ(retarget(1, 1000000, 10), 0u);
}

TEST(Retarget, NeverProducesAWraparoundNearZero) {
    uint32_t result = retarget(0, 999999999ULL, 1);
    EXPECT_LT(result, 1000u);
}


TEST(Retarget, DoesNotOverflowAtMaxDifficulty) {
    constexpr uint32_t kMax = std::numeric_limits<uint32_t>::max();
    uint32_t result = retarget(kMax, 1,10);
    EXPECT_EQ(result, kMax) << "difficulty overflowed instead of clamping at max";
}



TEST(Retarget, ZeroExpectedAndZeroActualIsNeutral) {
    EXPECT_EQ(retarget(10, 0, 0), 10u);
}

TEST(Retarget, ZeroExpectedAndAnyPositiveActualDecreases) {
    EXPECT_EQ(retarget(10,1, 0), 9u);
    EXPECT_EQ(retarget(10, 1000000,0), 9u);
}



TEST(Retarget, HandlesLargeRealisticTimeValuesWithoutCrashing) {
    constexpr uint64_t kFiftyYearsInSeconds = 50ULL * 365 * 24 * 3600;

    EXPECT_NO_THROW({
        auto result = retarget(20, kFiftyYearsInSeconds, 600);
        EXPECT_EQ(result, 19u);
    });
}



TEST(Retarget, IsDeterministicAcrossManyCalls) {
    for (int i = 0; i < 10000; ++i) {
        EXPECT_EQ(retarget(15, 3, 10), 16u) << "mismatch at iteration " << i;
    }
}

TEST(Retarget, RepeatedCallsWithSameArgsNeverDrift) {
    uint32_t first = retarget(7, 2, 10);
    for (int i = 0; i < 5000; ++i) {
        ASSERT_EQ(retarget(7, 2, 10), first) << "drifted at call " << i;
    }
}


TEST(Retarget, StepNeverExceedsOneInEitherDirectionAcrossManyInputs) {
    std::mt19937 rng(2024);
    std::uniform_int_distribution<uint32_t> difficultyDist(1, 1'000'000);
    std::uniform_int_distribution<uint64_t> timeDist(0, 1'000'000);

    constexpr int kIterations = 20000;
    for (int i = 0; i < kIterations; ++i) {
        uint32_t oldDifficulty = difficultyDist(rng);
        uint64_t actual = timeDist(rng);
        uint64_t expected = timeDist(rng);

        uint32_t newDifficulty = retarget(oldDifficulty, actual, expected);

        if (newDifficulty > oldDifficulty) {
            EXPECT_EQ(newDifficulty - oldDifficulty, 1u)
                << "unexpected jump at iteration " << i
                << " old=" << oldDifficulty << " actual=" << actual
                << " expected=" << expected;
        } else if (newDifficulty < oldDifficulty) {
            EXPECT_EQ(oldDifficulty - newDifficulty, 1u)
                << "unexpected drop at iteration " << i
                << " old=" << oldDifficulty << " actual=" << actual
                << " expected=" << expected;
        }
    }
}



TEST(Retarget, SustainedFastBlocksRaiseDifficultyMonotonicallyWithoutOverflowRisk) {
    uint32_t difficulty = 1;
    constexpr int kRounds = 1000;

    for (int i = 0; i < kRounds; ++i) {
        uint32_t next = retarget(difficulty, 1, 100);
        ASSERT_GE(next, difficulty) << "difficulty unexpectedly dropped at round " << i;
        difficulty = next;
    }

    EXPECT_EQ(difficulty, 1001u);
}

TEST(Retarget, SustainedSlowBlocksLowerDifficultyThenPlateauAtZero) {
    uint32_t difficulty = 50;
    constexpr int kRounds = 1000;

    for (int i = 0; i < kRounds; ++i) {
        uint32_t next = retarget(difficulty,1000000,10);
        ASSERT_LE(next, difficulty) << "difficulty unexpectedly rose at round " << i;
        difficulty = next;
    }

    EXPECT_EQ(difficulty, 0u);
}

TEST(Retarget, AlternatingFastAndSlowRoundsStayBounded) {
    uint32_t difficulty = 20;
    constexpr int kRounds = 5000;

    for (int i = 0; i < kRounds; ++i) {
        bool fastRound = (i % 2 == 0);
        uint64_t actual = fastRound ? 1 : 1000000;
        difficulty = retarget(difficulty, actual, 10);

        ASSERT_LE(difficulty, 25u) << "difficulty drifted too high at round " << i;
    }
}
