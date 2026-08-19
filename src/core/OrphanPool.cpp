#include "core/OrphanPool.hpp"
#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <optional>
#include <utility>
namespace forgechain::core {
void OrphanPool::add_orphan(Block &&block) {
  crypto::HashBytes hash = block.hash_;
  orphan_pool_.insert_or_assign(hash, std::move(block));
}

bool OrphanPool::has_orphan(const crypto::HashBytes &hash) const {
  return orphan_pool_.contains(hash);
}
std::optional<Block>
OrphanPool::find_orphan(const crypto::HashBytes &hash) const {
  auto it = orphan_pool_.find(hash);
  if (it == orphan_pool_.end())
    return std::nullopt;
  return it->second;
}
std::optional<Block>
OrphanPool::find_orphan_by_prev_hash(const crypto::HashBytes &prev_hash) const {
  for (const auto &block : orphan_pool_) {
    if (block.second.prev_hash_ == prev_hash) {
      return block.second;
    }
  }

  return std::nullopt;
}

void OrphanPool::remove_orphan(const crypto::HashBytes &hash) {
  orphan_pool_.erase(hash);
}

size_t OrphanPool::orphan_count() const { return orphan_pool_.size(); }
} // namespace forgechain::core
