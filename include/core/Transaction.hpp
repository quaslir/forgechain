#pragma once
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <optional>
namespace forgechain::core {
using forgechain::crypto::bytes;
using forgechain::crypto::str;

struct Transaction {
  str sender_;
  str recipient_;
  uint64_t amount_;
  bytes signature_;

  Transaction(str sender, str recipient, uint64_t amount);
  [[nodiscard]] bytes serialize() const;
  [[nodiscard]] static std::optional<Transaction> deserialize(const crypto::bytes& payload);
  [[nodiscard]] crypto::HashBytes compute_hash() const;
  bool operator==(const Transaction &tx);
};
} // namespace forgechain::core
