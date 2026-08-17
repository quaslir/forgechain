#pragma once

#include <cstddef>
#include <vector>

#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"
namespace forgechain::core {
using forgechain::core::Block;
class Blockchain {
public:
  Blockchain();

  void add_block(const Block &block);

  [[nodiscard]] const Block &latest() const;
  [[nodiscard]] const Block &at(size_t height) const;
  [[nodiscard]] bool has_block(const crypto::HashBytes &hash) const;
  [[nodiscard]] size_t size() const;

  [[nodiscard]] bool is_valid() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] const Block &operator[](size_t height) const;

private:
  std::vector<Block> blocks_;
};
} // namespace forgechain::core
