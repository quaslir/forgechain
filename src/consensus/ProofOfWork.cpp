#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
namespace forgechain::consensus {
using forgechain::crypto::HashBytes;
bool meets_target(const HashBytes &hash, uint32_t difficulty) {
  uint32_t result = 0;
  for (const auto &byte : hash) {

    uint8_t mask = 0x80;
    bool found_one = false;
    for (int i = 0; i < 8; i++) {
      if (!(byte & mask)) {
        result++;
        mask >>= 1;
      } else {
        found_one = true;
        break;
      }
    }
    if (found_one) {
      break;
    }
  }

  return result >= difficulty;
}

Block mine_block(uint32_t version, HashBytes prev_hash, uint64_t timestamp,
                 uint32_t difficulty,
                 std::vector<core::Transaction> transactions) {
  uint32_t nonce = 0;
  constexpr uint32_t kMaxNonce = std::numeric_limits<uint32_t>::max();
  Block block{version, prev_hash, timestamp, std::move(transactions)};
  block.difficulty_ = difficulty;
  while (nonce < kMaxNonce) {
    block.nonce_ = nonce;
    auto hash = block.compute_hash();
    if (meets_target(hash, difficulty)) {
      block.hash_ = hash;
      return block;
    }
    nonce++;
  }
  throw std::runtime_error(
      "mine_block: exhausted nonce range without finding a valid hash");
}

uint32_t retarget(uint32_t old_difficulty, uint64_t actual_time_seconds,
                  uint64_t expected_time_seconds) {
  if (actual_time_seconds == 0) {
    actual_time_seconds = 1;
  }
  if (expected_time_seconds == 0) {
    return old_difficulty;
  }
  double ratio = static_cast<double>(expected_time_seconds) /
                 static_cast<double>(actual_time_seconds);
  double delta_bits = std::log2(ratio);
  long long rounded_delta = std::llround(delta_bits);
  constexpr long long kMaxStepBits = 2;
  if (rounded_delta > kMaxStepBits)
    rounded_delta = kMaxStepBits;
  else if (rounded_delta < -kMaxStepBits)
    rounded_delta = -kMaxStepBits;

  if (rounded_delta > 0) {
    uint64_t new_difficulty = static_cast<uint64_t>(old_difficulty) +
                              static_cast<uint64_t>(rounded_delta);
    if (new_difficulty > std::numeric_limits<uint32_t>::max()) {
      new_difficulty = std::numeric_limits<uint32_t>::max();
    }

    return static_cast<uint32_t>(new_difficulty);
  } else if (rounded_delta < 0) {
    auto decrease = static_cast<uint64_t>(-rounded_delta);
    if (decrease >= old_difficulty) {
      return 0;
    }

    return old_difficulty - static_cast<uint32_t>(decrease);
  }

  return old_difficulty;
}
} // namespace forgechain::consensus
