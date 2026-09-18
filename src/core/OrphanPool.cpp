#include "core/OrphanPool.hpp"
#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>
namespace forgechain::core {
OrphanPool::OrphanPool(size_t max_size) : max_size_(max_size) {}
void OrphanPool::add_orphan(Block &&block) {
  crypto::HashBytes hash = block.hash_;
  if (orphan_pool_.contains(hash)) {
    orphan_pool_.at(hash).block = std::move(block);
    return;
  }
  if (orphan_count() >= max_size_) {
    auto oldest = orphan_pool_.find(order_.front());
    if (oldest != orphan_pool_.end()) {
      orphan_pool_.erase(oldest);
    }
    order_.pop_front();
  }

  order_.push_back(hash);
  orphan_pool_.emplace(
      hash, Entry{.block = std::move(block), .pos = std::prev(order_.end())});
}

bool OrphanPool::has_orphan(const crypto::HashBytes &hash) const {
  return orphan_pool_.contains(hash);
}
std::optional<Block>
OrphanPool::find_orphan(const crypto::HashBytes &hash) const {
  auto it = orphan_pool_.find(hash);
  if (it == orphan_pool_.end())
    return std::nullopt;
  return it->second.block;
}
std::optional<Block>
OrphanPool::find_orphan_by_prev_hash(const crypto::HashBytes &prev_hash) const {
  for (const auto &block : orphan_pool_) {
    if (block.second.block.prev_hash_ == prev_hash) {
      return block.second.block;
    }
  }

  return std::nullopt;
}

void OrphanPool::remove_orphan(const crypto::HashBytes &hash) {
  auto it = orphan_pool_.find(hash);
  if (it == orphan_pool_.end())
    return;
  order_.erase(it->second.pos);
  orphan_pool_.erase(it);
}

size_t OrphanPool::orphan_count() const { return orphan_pool_.size(); }

std::vector<Block>
OrphanPool::children_of(const crypto::HashBytes &hash) const {
  std::vector<Block> children;
  for (const auto &block : orphan_pool_) {
    if (block.second.block.prev_hash_ == hash) {
      children.push_back(block.second.block);
    }
  }

  return children;
}
} // namespace forgechain::core
