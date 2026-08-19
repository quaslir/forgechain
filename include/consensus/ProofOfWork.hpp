#pragma once

#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <vector>
namespace forgechain::consensus {
using forgechain::core::Block;
using forgechain::crypto::HashBytes;
bool meets_target(const HashBytes &hash, uint32_t difficulty);
Block mine_block(uint32_t version, HashBytes prev_hash, uint64_t timestamp,
                 uint32_t difficulty,
                 std::vector<core::Transaction> transactions);
uint32_t retarget(uint32_t old_difficulty, uint64_t actual_time_seconds,
                  uint64_t expected_time_seconds);
  uint64_t block_work(uint32_t difficulty);
} // namespace forgechain::consensus
