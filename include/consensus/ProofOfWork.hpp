#pragma once

#include "consensus/ConsensusParams.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
namespace forgechain::consensus {
using forgechain::core::Block;
using forgechain::crypto::HashBytes;
static constexpr uint64_t mining_reward = 50;
bool meets_target(const HashBytes &hash, uint32_t difficulty);
Block mine_block(uint32_t version, HashBytes prev_hash, uint64_t timestamp,
                 uint32_t difficulty,
                 std::vector<core::Transaction> transactions);
uint32_t retarget(uint32_t old_difficulty, uint64_t actual_time_seconds,
                  uint64_t expected_time_seconds);
uint64_t block_work(uint32_t difficulty);
bool validate_coinbase_amount(const std::vector<core::Transaction> &txs);
uint32_t
next_difficulty(size_t height, const ConsensusParams &params,
                const std::function<const core::Block &(size_t)> &block_at);
uint64_t
median_time_past(size_t height, size_t window,
                 const std::function<const core::Block &(size_t)> &block_at);
bool timestamp_is_valid(
    const core::Block &block, size_t height, uint64_t now,
    const ConsensusParams &params,
    const std::function<const core::Block &(size_t)> &block_at);
} // namespace forgechain::consensus
