#include "core/Ledger.hpp"
#include "core/Transaction.hpp"
#include <cstdint>
#include <optional>
namespace forgechain::core {
void Ledger::set_balance(const core::str &address, uint64_t amount) {
  balances_[address] = amount;
}

std::optional<uint64_t> Ledger::get_balance(const core::str &address) const {
  if (balances_.contains(address)) {
    return balances_.at(address);
  }
  return std::nullopt;
}

bool Ledger::apply_transaction(const Transaction &tx) {
  if (!balances_.contains(tx.sender_))
    return false;
  if (tx.amount_ > balances_[tx.sender_])
    return false;

  balances_[tx.sender_] -= tx.amount_;
  balances_[tx.recipient_] += tx.amount_;
  return true;
}
bool Ledger::reverse_transaction(const Transaction& tx) {
    if (!balances_.contains(tx.recipient_))
      return false;
    if (tx.amount_ > balances_[tx.recipient_])
      return false;
    balances_[tx.sender_] += tx.amount_;
    balances_[tx.recipient_] -= tx.amount_;
    return true;
}
} // namespace forgechain::core
