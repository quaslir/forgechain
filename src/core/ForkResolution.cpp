#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "core/Blockchain.hpp"
#include "core/OrphanPool.hpp"
#include "core/Block.hpp"
#include <utility>
#include "core/ForkResolution.hpp"
#include <algorithm>
#include "crypto/CommonTypes.hpp"
namespace forgechain::core {
    std::optional<ForkChain> build_fork_chain(const Blockchain& chain, const OrphanPool& pool, const Block& tip) {
        std::vector<Block> fork_chain{tip};
        HashBytes current_hash = tip.prev_hash_;
        size_t depth = 0;
        for(;;) {
            if(depth >= kMaxForkDepth) {
                return std::nullopt;
            }
            auto found_in_chain = chain.find(current_hash);
            if(found_in_chain.has_value()) {
                std::reverse(fork_chain.begin(), fork_chain.end());
                return {ForkChain{.blocks = std::move(fork_chain), .common_ancestor = std::move(*found_in_chain)}};
            }

            auto found_in_pool = pool.find_orphan(current_hash);
            if(!found_in_pool.has_value()) {
                return std::nullopt;
            }
            fork_chain.push_back(*found_in_pool);
            current_hash = found_in_pool->prev_hash_;
            depth++;
        }
    }
    bool is_fork_heavier(const Blockchain& chain, const ForkChain& fork) {
        uint64_t fork_work =  fork.common_ancestor.cumulative_work_;
        for(const auto& block : fork.blocks) {
            fork_work += block.block_work();
        }

        uint64_t chain_work = chain.latest().cumulative_work_;

        return fork_work > chain_work;

    }
}
