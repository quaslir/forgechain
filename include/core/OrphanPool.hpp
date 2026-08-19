#pragma once
#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"
#include <optional>
#include <cstddef>
#include <unordered_map>
namespace forgechain::core {
    class OrphanPool {
        public:
              void add_orphan(Block && block);
              [[nodiscard]] bool has_orphan(const crypto::HashBytes& hash) const;
              [[nodiscard]] std::optional<Block> find_orphan(const crypto::HashBytes& hash) const;
              [[nodiscard]] std::optional<Block> find_orphan_by_prev_hash(const crypto::HashBytes& prev_hash) const;

              void remove_orphan(const crypto::HashBytes& hash);

              [[nodiscard]] size_t orphan_count() const;

        private:
              std::unordered_map<crypto::HashBytes, Block, crypto::HashBytesHasher> orphan_pool_;

    };


}
