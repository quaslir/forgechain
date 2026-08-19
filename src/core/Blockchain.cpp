#include "core/Blockchain.hpp"
#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
namespace forgechain::core {
using forgechain::core::Block;
using forgechain::crypto::HashBytes;
Blockchain::Blockchain() {
  Block genesis{1, HashBytes{}, 0, {}};
  blocks_.push_back(genesis);
}

void Blockchain::add_block(Block &&block) {
    uint64_t prev_cumulative_work = size() > 0 ? blocks_.back().cumulative_work_ : 0;
    uint64_t current_cumulative_work = block.block_work() + prev_cumulative_work;
    block.cumulative_work_ = current_cumulative_work;
    blocks_.push_back(std::move(block));
}
bool Blockchain::has_block(const crypto::HashBytes &hash) const {
  auto it =
      std::find_if(blocks_.begin(), blocks_.end(),
                   [&hash](const Block &block) { return block.hash_ == hash; });

  return it != blocks_.end();
}


std::optional<Block> Blockchain::find(const crypto::HashBytes &hash) const {
  for (const auto &block : blocks_) {
    if (block.hash_ == hash) {
      return block;
    }
  }
  return std::nullopt;
}

const Block &Blockchain::at(size_t height) const { return blocks_.at(height); }

const Block &Blockchain::latest() const { return blocks_.back(); }

size_t Blockchain::size() const { return blocks_.size(); }

[[nodiscard]] bool Blockchain::is_valid() const {
  for (size_t height = 1; height < blocks_.size(); height++) {
    const auto &prev_block = blocks_[height - 1];
    const auto &current_block = blocks_[height];
    if (current_block.compute_hash() != current_block.hash_)
      return false;
    if (prev_block.hash_ != current_block.prev_hash_)
      return false;
  }

  return true;
}
[[nodiscard]] bool Blockchain::empty() const { return blocks_.size() == 0; }
[[nodiscard]] const Block &Blockchain::operator[](size_t height) const {
  return blocks_[height];
}
} // namespace forgechain::core
