#include "consensus/ProofOfWork.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>
using namespace forgechain::consensus;

TEST(Retarget, UnchangedWhenExactlyOnTarget) {
    EXPECT_EQ(retarget(10, 10, 10), 10u);
}

TEST(Retarget, IncreasesByOneBitWhenTwiceAsFast) {
    EXPECT_EQ(retarget(10, 5, 10), 11u);
}

TEST(Retarget, IncreasesByTwoBitsWhenFourTimesAsFast) {
    EXPECT_EQ(retarget(10, 5, 20), 12u);
}

TEST(Retarget, DecreasesByOneBitWhenTwiceAsSlow) {
    EXPECT_EQ(retarget(10, 20, 10), 9u);
}

TEST(Retarget, DecreasesByTwoBitsWhenFourTimesAsSlow) {
    EXPECT_EQ(retarget(10, 20, 5), 8u);
}

TEST(Retarget, ClampsToPlusTwoWhenFarMoreThanFourTimesFaster) {
    EXPECT_EQ(retarget(10, 1, 8), 12u);
}

TEST(Retarget, ClampsToPlusTwoAtExtremeRatio) {
    EXPECT_EQ(retarget(10, 1, 1000000), 12u);
}

TEST(Retarget, ClampsToMinusTwoWhenFarMoreThanFourTimesSlower) {
    EXPECT_EQ(retarget(10, 80, 10), 8u);
}

TEST(Retarget, ClampsToMinusTwoAtExtremeRatio) {
    EXPECT_EQ(retarget(10, 1000000, 1), 8u);
}

TEST(Retarget, RatioOnePointFiveRoundsUpToPlusOne) {
    EXPECT_EQ(retarget(10, 10, 15), 11u);
}

TEST(Retarget, RatioThreeQuartersRoundsToZero) {
    EXPECT_EQ(retarget(10, 20, 15), 10u);
}

TEST(Retarget, RatioThreeRoundsToPlusTwo) {
    EXPECT_EQ(retarget(10, 10, 30), 12u);
}

TEST(Retarget, ZeroActualTimeIsTreatedAsOneSecond) {
    EXPECT_EQ(retarget(10, 0, 100), retarget(10, 1, 100));
}

TEST(Retarget, ZeroActualTimeDoesNotCrashOrProduceNaN) {
    EXPECT_NO_THROW({
        uint32_t result = retarget(10, 0, 100);
        EXPECT_EQ(result, 12u);
    });
}

TEST(Retarget, ZeroExpectedTimeLeavesDifficultyUnchanged) {
    EXPECT_EQ(retarget(10, 5, 0), 10u);
    EXPECT_EQ(retarget(10, 999999, 0), 10u);
    EXPECT_EQ(retarget(0, 5, 0), 0u);
}

TEST(Retarget, BothZeroLeavesDifficultyUnchanged) {
    EXPECT_EQ(retarget(10, 0, 0), 10u);
}

TEST(Retarget, StaysAtZeroWhenAlreadyZeroAndBlocksAreSlow) {
    EXPECT_EQ(retarget(0, 1000000, 10), 0u);
}

TEST(Retarget, DropsFromOneToZeroNotBelow) {
    EXPECT_EQ(retarget(1, 1000000, 10), 0u);
}

TEST(Retarget, DropsFromTwoByClampedTwoLandsExactlyAtZero) {
    EXPECT_EQ(retarget(2, 1000000, 10), 0u);
}

TEST(Retarget, NeverWrapsToHugeValueNearZero) {
    uint32_t result = retarget(0, 999999999ULL, 1);
    EXPECT_LT(result, 1000u);
}

TEST(Retarget, DoesNotOverflowAtMaxDifficulty) {
    constexpr uint32_t kMax = std::numeric_limits<uint32_t>::max();
    EXPECT_EQ(retarget(kMax, 1, 1000), kMax);
}

TEST(Retarget, StaysBoundedNearMaxDifficultyEvenWithFastBlocks) {
    constexpr uint32_t kNearMax = std::numeric_limits<uint32_t>::max() - 1;
    uint32_t result = retarget(kNearMax, 1, 1000);
    EXPECT_LE(result, std::numeric_limits<uint32_t>::max());
    EXPECT_GE(result, kNearMax);
}

TEST(Retarget, HandlesLargeRealisticTimeValuesWithoutCrashing) {
    constexpr uint64_t kFiftyYearsInSeconds = 50ULL * 365 * 24 * 3600;

    EXPECT_NO_THROW({
        auto result = retarget(20, kFiftyYearsInSeconds, 600);

        EXPECT_EQ(result, 18u);
    });
}

TEST(Retarget, IsDeterministicAcrossManyCalls) {
    for (int i = 0; i < 10000; ++i) {
        EXPECT_EQ(retarget(15, 5, 20), 17u) << "mismatch at iteration " << i;
    }
}

TEST(Retarget, RepeatedCallsWithSameArgsNeverDrift) {
    uint32_t first = retarget(7, 3, 10);
    for (int i = 0; i < 5000; ++i) {
        ASSERT_EQ(retarget(7, 3, 10), first) << "drifted at call " << i;
    }
}

TEST(Retarget, StepNeverExceedsClampAcrossManyRandomInputs) {
    std::mt19937 rng(2024);
    std::uniform_int_distribution<uint32_t> difficultyDist(1, 1'000'000);
    std::uniform_int_distribution<uint64_t> timeDist(1, 1'000'000);

    constexpr int kIterations = 20000;
    constexpr uint32_t kMaxStep = 2;

    for (int i = 0; i < kIterations; ++i) {
        uint32_t oldDifficulty = difficultyDist(rng);
        uint64_t actual = timeDist(rng);
        uint64_t expected = timeDist(rng);

        uint32_t newDifficulty = retarget(oldDifficulty, actual, expected);

        uint32_t step = (newDifficulty > oldDifficulty)
                             ? (newDifficulty - oldDifficulty)
                             : (oldDifficulty - newDifficulty);

        EXPECT_LE(step, kMaxStep)
            << "step exceeded clamp at iteration " << i << " old=" << oldDifficulty
            << " actual=" << actual << " expected=" << expected << " new=" << newDifficulty;
    }
}

TEST(Retarget, SustainedExtremeFastBlocksClimbByExactlyTwoPerRound) {
    uint32_t difficulty = 1;
    constexpr int kRounds = 1000;

    for (int i = 0; i < kRounds; ++i) {
        uint32_t next = retarget(difficulty, 1, 100);
        ASSERT_EQ(next - difficulty, 2u) << "round " << i << " did not step by exactly +2";
        difficulty = next;
    }

    EXPECT_EQ(difficulty, 2001u);
}

TEST(Retarget, SustainedExtremeSlowBlocksDropThenPlateauAtZero) {
    uint32_t difficulty = 50;
    constexpr int kRounds = 1000;

    for (int i = 0; i < kRounds; ++i) {
        uint32_t next = retarget(difficulty, 1000000, 10);
        ASSERT_LE(next, difficulty) << "difficulty unexpectedly rose at round " << i;
        difficulty = next;
    }

    EXPECT_EQ(difficulty, 0u);
}

TEST(Retarget, AlternatingExtremeFastAndSlowRoundsStayBounded) {
    uint32_t difficulty = 20;
    constexpr int kRounds = 5000;

    for (int i = 0; i < kRounds; ++i) {
        bool fastRound = (i % 2 == 0);
        uint64_t actual = fastRound ? 1 : 1000000;
        difficulty = retarget(difficulty, actual, 10);

        ASSERT_LE(difficulty, 24u) << "difficulty drifted too high at round " << i;
    }
}
