#pragma once
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace forgechain::core {
class Ledger {
public:
  void set_balance(const crypto::str &address, uint64_t amount);
  [[nodiscard]] std::optional<uint64_t>
  get_balance(const crypto::str &address) const;

  bool apply_transaction(const Transaction &tx);
  bool reverse_transaction(const Transaction &tx);
  [[nodiscard]] std::vector<std::pair<crypto::str, uint64_t>>
  all_balances() const;

private:
  std::unordered_map<str, uint64_t> balances_;
};
} // namespace forgechain::core
