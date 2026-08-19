#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>
#include "core/Blockchain.hpp"
#include "core/OrphanPool.hpp"
#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"
#include "core/ForkResolution.hpp"
#include <algorithm>
namespace forgechain::core {
    std::optional<std::vector<Block>> build_fork_chain(const Blockchain& chain, const OrphanPool& pool, const Block& tip) {
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
                return fork_chain;
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
}
