#pragma once
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <vector>
namespace forgechain::core {
class Mempool {
public:
  bool add_transaction(const Transaction &tx,
                       const crypto::bytes &sender_public_key);
  void remove_transaction(const Transaction &tx);
  [[nodiscard]] std::vector<Transaction>
  get_transactions_for_block(size_t limit) const;
  [[nodiscard]] size_t size() const;
  [[nodiscard]] bool empty() const;

private:
  std::vector<Transaction> pending_;
};
} // namespace forgechain::core
