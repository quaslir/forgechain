#pragma once
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/OrphanPool.hpp"
#include <cstddef>
#include <optional>
#include <vector>
namespace forgechain::core {
struct ForkChain {
  std::vector<Block> blocks;
  Block common_ancestor;
};
constexpr size_t kMaxForkDepth = 100;
std::optional<ForkChain> build_fork_chain(const Blockchain &chain,
                                          const OrphanPool &pool,
                                          const Block &tip);
bool is_fork_heavier(const Blockchain &chain, const ForkChain &fork);
} // namespace forgechain::core
