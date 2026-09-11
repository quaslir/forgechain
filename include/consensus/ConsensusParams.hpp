#pragma once

#include <cstddef>
#include <cstdint>
namespace forgechain::consensus {
struct ConsensusParams {
  uint32_t initial_difficulty;
  uint32_t min_difficulty;
  uint64_t target_block_time;
  size_t retarget_interval;
  size_t mtp_window;
  uint64_t max_future_drift;
};

inline constexpr ConsensusParams kMainParams{
    .initial_difficulty = 15, .min_difficulty = 8, .target_block_time = 10,
    .retarget_interval = 20, .mtp_window = 11, .max_future_drift = 120};

inline constexpr ConsensusParams kTestParams{
    .initial_difficulty = 4, .min_difficulty = 1, .target_block_time = 10,
    .retarget_interval = 20, .mtp_window = 11, .max_future_drift = 120};
} // namespace forgechain::consensus
