#include "consensus/ConsensusParams.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

using forgechain::core::Block;
using forgechain::crypto::HashBytes;
using namespace forgechain::consensus;

namespace {

constexpr uint32_t kInitial = 10;
constexpr uint32_t kMin = 4;
constexpr uint64_t kTargetTime = 10;
constexpr size_t kInterval = 20;

const ConsensusParams kParams{.initial_difficulty = kInitial,
                              .min_difficulty = kMin,
                              .target_block_time = kTargetTime,
                              .retarget_interval = kInterval,
                              .mtp_window = 11,
                              .max_future_drift = 120};

std::vector<Block> make_chain(size_t count, uint64_t spacing,
                              uint32_t difficulty = kInitial) {
  std::vector<Block> chain;
  chain.reserve(count);
  for (size_t h = 0; h < count; h++) {
    uint64_t timestamp = h == 0 ? 0 : 1000 + h * spacing;
    Block block{1, HashBytes{}, timestamp, {}};
    block.difficulty_ = difficulty;
    chain.push_back(block);
  }
  return chain;
}

std::function<const Block &(size_t)> accessor(const std::vector<Block> &chain) {
  return [&chain](size_t h) -> const Block & { return chain.at(h); };
}

} // namespace

TEST(NextDifficulty, FirstEpochUsesInitial) {
  auto chain = make_chain(kInterval, 1, 99);
  EXPECT_EQ(next_difficulty(1, kParams, accessor(chain)), kInitial);
  EXPECT_EQ(next_difficulty(kInterval, kParams, accessor(chain)), kInitial);
}

TEST(NextDifficulty, NoRetargetBeforeSecondEpoch) {
  auto chain = make_chain(2 * kInterval, 1);
  EXPECT_EQ(next_difficulty(kInterval + 1, kParams, accessor(chain)), kInitial);
  EXPECT_EQ(next_difficulty(2 * kInterval - 1, kParams, accessor(chain)),
            kInitial);
}

TEST(NextDifficulty, InsideEpochCopiesParent) {
  auto chain = make_chain(2 * kInterval + 1, kTargetTime);
  chain[2 * kInterval].difficulty_ = kInitial + 3;
  EXPECT_EQ(next_difficulty(2 * kInterval + 1, kParams, accessor(chain)),
            kInitial + 3);
}

TEST(NextDifficulty, UnchangedWhenOnTarget) {
  auto chain = make_chain(2 * kInterval, kTargetTime);
  EXPECT_EQ(next_difficulty(2 * kInterval, kParams, accessor(chain)), kInitial);
}

TEST(NextDifficulty, IncreasesWhenTwiceAsFast) {
  auto chain = make_chain(2 * kInterval, kTargetTime / 2);
  EXPECT_EQ(next_difficulty(2 * kInterval, kParams, accessor(chain)),
            kInitial + 1);
}

TEST(NextDifficulty, DecreasesWhenTwiceAsSlow) {
  auto chain = make_chain(2 * kInterval, kTargetTime * 2);
  EXPECT_EQ(next_difficulty(2 * kInterval, kParams, accessor(chain)),
            kInitial - 1);
}

TEST(NextDifficulty, IncreaseClampedToTwoBits) {
  auto chain = make_chain(2 * kInterval, 1);
  EXPECT_EQ(next_difficulty(2 * kInterval, kParams, accessor(chain)),
            kInitial + 2);
}

TEST(NextDifficulty, DecreaseClampedToTwoBits) {
  auto chain = make_chain(2 * kInterval, kTargetTime * 10);
  EXPECT_EQ(next_difficulty(2 * kInterval, kParams, accessor(chain)),
            kInitial - 2);
}

TEST(NextDifficulty, NeverDropsBelowMin) {
  ConsensusParams params = kParams;
  params.initial_difficulty = kMin;
  auto chain = make_chain(2 * kInterval, kTargetTime * 10, kMin);
  EXPECT_EQ(next_difficulty(2 * kInterval, params, accessor(chain)), kMin);
}

TEST(NextDifficulty, NonMonotonicTimestampsDoNotUnderflow) {
  auto chain = make_chain(2 * kInterval, kTargetTime);
  chain[2 * kInterval - 1].timestamp_ = chain[kInterval].timestamp_ - 5;
  // actual collapses to 0 -> treated as "too fast", not as a huge duration.
  EXPECT_EQ(next_difficulty(2 * kInterval, kParams, accessor(chain)),
            kInitial + 2);
}

TEST(NextDifficulty, IntervalBelowTwoAlwaysReturnsInitial) {
  auto chain = make_chain(10, 1, 99);
  for (size_t interval : {size_t{0}, size_t{1}}) {
    ConsensusParams params = kParams;
    params.retarget_interval = interval;
    for (size_t height = 1; height < chain.size(); height++) {
      EXPECT_EQ(next_difficulty(height, params, accessor(chain)), kInitial)
          << "interval=" << interval << " height=" << height;
    }
  }
}

TEST(NextDifficulty, ReadsOnlyThePreviousEpoch) {
  auto chain = make_chain(2 * kInterval, kTargetTime);
  size_t lowest = std::numeric_limits<size_t>::max();
  size_t highest = 0;
  std::function<const Block &(size_t)> tracking =
      [&](size_t h) -> const Block & {
    lowest = std::min(lowest, h);
    highest = std::max(highest, h);
    return chain.at(h);
  };

  next_difficulty(2 * kInterval, kParams, tracking);

  EXPECT_EQ(lowest, kInterval);
  EXPECT_EQ(highest, 2 * kInterval - 1);
}

TEST(NextDifficulty, MidEpochReadsOnlyParent) {
  auto chain = make_chain(2 * kInterval + 5, kTargetTime);
  std::vector<size_t> requested;
  std::function<const Block &(size_t)> tracking =
      [&](size_t h) -> const Block & {
    requested.push_back(h);
    return chain.at(h);
  };

  next_difficulty(2 * kInterval + 5, kParams, tracking);

  ASSERT_EQ(requested.size(), 1u);
  EXPECT_EQ(requested[0], 2 * kInterval + 4);
}
