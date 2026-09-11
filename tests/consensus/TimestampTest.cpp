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

constexpr size_t kWindow = 11;
constexpr uint64_t kDrift = 120;
constexpr uint64_t kNow = 1'700'000'000;

const ConsensusParams kParams{.initial_difficulty = 10,
                              .min_difficulty = 4,
                              .target_block_time = 10,
                              .retarget_interval = 20,
                              .mtp_window = kWindow,
                              .max_future_drift = kDrift};

std::vector<Block> chain_from(const std::vector<uint64_t> &timestamps) {
  std::vector<Block> chain;
  chain.reserve(timestamps.size());
  for (uint64_t ts : timestamps) {
    chain.emplace_back(1, HashBytes{}, ts, std::vector<forgechain::core::Transaction>{});
  }
  return chain;
}

std::function<const Block &(size_t)> accessor(const std::vector<Block> &chain) {
  return [&chain](size_t h) -> const Block & { return chain.at(h); };
}

Block block_at_time(uint64_t ts) {
  return Block{1, HashBytes{}, ts, {}};
}

} // namespace


TEST(MedianTimePast, EmptyWindowReturnsZero) {
  auto chain = chain_from({100, 200, 300});
  EXPECT_EQ(median_time_past(0, kWindow, accessor(chain)), 0u);
  EXPECT_EQ(median_time_past(3, 0, accessor(chain)), 0u);
}

TEST(MedianTimePast, GenesisOnly) {
  auto chain = chain_from({0});
  EXPECT_EQ(median_time_past(1, kWindow, accessor(chain)), 0u);
}

TEST(MedianTimePast, EvenCountTakesUpperMiddle) {
  auto chain = chain_from({0, 100});
  EXPECT_EQ(median_time_past(2, kWindow, accessor(chain)), 100u);
}

TEST(MedianTimePast, ShortChainUsesAllBlocks) {
  auto chain = chain_from({0, 100, 50});
  EXPECT_EQ(median_time_past(3, kWindow, accessor(chain)), 50u);
}

TEST(MedianTimePast, FullWindowUsesOnlyLastBlocks) {
  std::vector<uint64_t> ts;
  for (uint64_t h = 0; h < 20; h++)
    ts.push_back(h * 10);
  auto chain = chain_from(ts);
  EXPECT_EQ(median_time_past(20, kWindow, accessor(chain)), 140u);
}

TEST(MedianTimePast, UnorderedTimestampsTakeMedianNotLatest) {
  auto chain = chain_from({500, 100, 900, 300, 700});
  EXPECT_EQ(median_time_past(5, kWindow, accessor(chain)), 500u);
}

TEST(MedianTimePast, IgnoresBlocksAtOrAboveHeight) {
  // Blocks at height >= 3 exist in the vector but must not be read.
  auto chain = chain_from({0, 100, 50, 99999, 99999});
  EXPECT_EQ(median_time_past(3, kWindow, accessor(chain)), 50u);
}

TEST(MedianTimePast, ReadsExactlyTheWindow) {
  std::vector<uint64_t> ts(30, 1000);
  auto chain = chain_from(ts);
  std::vector<size_t> requested;
  std::function<const Block &(size_t)> tracking =
      [&](size_t h) -> const Block & {
    requested.push_back(h);
    return chain.at(h);
  };

  (void)median_time_past(25, kWindow, tracking);

  ASSERT_EQ(requested.size(), kWindow);
  EXPECT_EQ(*std::min_element(requested.begin(), requested.end()), 14u);
  EXPECT_EQ(*std::max_element(requested.begin(), requested.end()), 24u);
}

// ---------------------------------------------------------- timestamp

TEST(TimestampIsValid, EqualToMtpIsRejected) {
  auto chain = chain_from({0, kNow - 100, kNow - 50});
  uint64_t mtp = median_time_past(3, kWindow, accessor(chain));
  EXPECT_FALSE(timestamp_is_valid(block_at_time(mtp), 3, kNow, kParams, accessor(chain)));
}

TEST(TimestampIsValid, OneAboveMtpIsAccepted) {
  auto chain = chain_from({0, kNow - 100, kNow - 50});
  uint64_t mtp = median_time_past(3, kWindow, accessor(chain));
  EXPECT_TRUE(timestamp_is_valid(block_at_time(mtp + 1), 3, kNow, kParams, accessor(chain)));
}

TEST(TimestampIsValid, BelowMtpIsRejected) {
  auto chain = chain_from({0, kNow - 100, kNow - 50});
  EXPECT_FALSE(timestamp_is_valid(block_at_time(kNow - 1000), 3, kNow, kParams, accessor(chain)));
}

TEST(TimestampIsValid, SameSecondBlocksEventuallyRejected) {
  // Two blocks already at kNow: the median of {0, kNow, kNow} is kNow,
  // so a third block in the same second no longer fits.
  auto chain = chain_from({0, kNow, kNow});
  EXPECT_FALSE(timestamp_is_valid(block_at_time(kNow), 3, kNow, kParams, accessor(chain)));
}

TEST(TimestampIsValid, ExactlyAtDriftIsAccepted) {
  auto chain = chain_from({0});
  EXPECT_TRUE(timestamp_is_valid(block_at_time(kNow + kDrift), 1, kNow, kParams, accessor(chain)));
}

TEST(TimestampIsValid, OnePastDriftIsRejected) {
  auto chain = chain_from({0});
  EXPECT_FALSE(timestamp_is_valid(block_at_time(kNow + kDrift + 1), 1, kNow, kParams, accessor(chain)));
}

TEST(TimestampIsValid, SameBlockBecomesValidWhenClockCatchesUp) {
  auto chain = chain_from({0});
  Block future = block_at_time(kNow + kDrift + 60);
  EXPECT_FALSE(timestamp_is_valid(future, 1, kNow, kParams, accessor(chain)));
  EXPECT_TRUE(timestamp_is_valid(future, 1, kNow + 60, kParams, accessor(chain)));
}

TEST(TimestampIsValid, NowNearMaxDoesNotWrap) {
  constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();
  auto chain = chain_from({0});
  EXPECT_TRUE(timestamp_is_valid(block_at_time(kMax), 1, kMax - 10, kParams, accessor(chain)));
  EXPECT_TRUE(timestamp_is_valid(block_at_time(kMax - 20), 1, kMax - 10, kParams, accessor(chain)));
}

TEST(TimestampIsValid, FutureCheckDoesNotReadChain) {
  auto chain = chain_from({0});
  size_t reads = 0;
  std::function<const Block &(size_t)> counting = [&](size_t h) -> const Block & {
    reads++;
    return chain.at(h);
  };
  EXPECT_FALSE(timestamp_is_valid(block_at_time(kNow + kDrift + 1), 1, kNow, kParams, counting));
  EXPECT_EQ(reads, 0u);
}
