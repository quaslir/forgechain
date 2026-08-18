#pragma once
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <optional>
#include <vector>
namespace forgechain::core {

using forgechain::crypto::HashBytes;
struct Block {
  uint32_t version_;
  HashBytes prev_hash_;
  HashBytes merkle_root_;
  uint64_t timestamp_;
  uint32_t difficulty_;
  uint32_t nonce_;

  std::vector<Transaction> transactions_;
  HashBytes hash_;

  Block(uint32_t version, HashBytes prev_hash, uint64_t timestamp,
        std::vector<Transaction> transactions);

  [[nodiscard]] HashBytes compute_hash() const;
  [[nodiscard]] std::vector<uint8_t> serialize() const;

  static std::optional<Block> deserialize(const crypto::bytes &payload);
};
} // namespace forgechain::core
