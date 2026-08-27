#pragma once
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <optional>
#include <set>
#include <vector>
namespace forgechain::core {
class Mempool {
public:

    Mempool(size_t max_size);

  bool add_transaction(const Transaction &tx,
                       const crypto::bytes &sender_public_key);
  void remove_transaction(const Transaction &tx);
  [[nodiscard]] bool has_transaction(const crypto::HashBytes &hash) const;
  [[nodiscard]] std::optional<Transaction>
  find(const crypto::HashBytes &hash) const;
  [[nodiscard]] std::vector<Transaction>
  get_transactions_for_block(size_t limit) const;
  [[nodiscard]] size_t size() const;
  [[nodiscard]] bool empty() const;

private:
    struct FeeDescendingComparator {
        bool operator()(const Transaction&a, const Transaction& b) const {
            return a.fee_ > b.fee_;
        }
    };


  std::multiset<Transaction, FeeDescendingComparator> pending_;
  size_t max_size_;
};
} // namespace forgechain::core
