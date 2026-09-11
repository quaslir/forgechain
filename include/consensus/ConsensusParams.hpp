#pragma once

#include <cstddef>
#include <cstdint>
namespace forgechain::consensus {
struct ConsensusParams {
    uint32_t initial_difficulty;
    uint32_t min_difficulty;
    uint64_t target_block_time;
    size_t retarget_interval;
};
}
