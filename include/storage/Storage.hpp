#pragma once
#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"
#include "storage/SqliteConnection.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace forgechain::storage {
class Storage {
public:
  explicit Storage(const std::string &path);

  void save_block(const core::Block &block, size_t height);
  [[nodiscard]] std::optional<core::Block> load_block(size_t height) const;
  [[nodiscard]] size_t block_count() const;

  void save_balance(const crypto::str &address, uint64_t amount);
  [[nodiscard]] std::optional<uint64_t>
  load_balance(const crypto::str &address) const;
  [[nodiscard]] std::vector<std::pair<crypto::str, uint64_t>>
  load_all_balances() const;

private:
  void create_schema();
  mutable SqliteConnection connection_;
};
} // namespace forgechain::storage
