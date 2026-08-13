#pragma once

#include "crypto/Hash.hpp"
#include "core/Block.hpp"
#include <cstdint>

namespace forgechain::consensus {
    using forgechain::crypto::HashBytes;
    using forgechain::core::Block;
    bool meets_target(const HashBytes& hash, uint32_t difficulty);
    Block mine_block(uint32_t version, HashBytes prev_hash, uint64_t timestamp, uint32_t difficulty);
    uint32_t retarget(uint32_t old_difficulty,
                       uint64_t actual_time_seconds,
                       uint64_t expected_time_seconds);
}
