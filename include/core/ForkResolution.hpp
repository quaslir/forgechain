#pragma once
#include <optional>
#include <vector>
#include "core/Blockchain.hpp"
#include "core/OrphanPool.hpp"
#include "core/Block.hpp"
namespace forgechain::core {
    constexpr size_t kMaxForkDepth = 100;
    std::optional<std::vector<Block>> build_fork_chain(const Blockchain& chain, const OrphanPool& pool, const Block& tip);
}
