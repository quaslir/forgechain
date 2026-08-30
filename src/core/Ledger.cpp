#include "core/Ledger.hpp"
#include "core/Transaction.hpp"
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
#include "crypto/CommonTypes.hpp"
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
  if (tx.sender_ != kCoinbaseSender) {
    if (!balances_.contains(tx.sender_))
      return false;
    if (tx.amount_ + tx.fee_ > balances_[tx.sender_])
      return false;

    balances_[tx.sender_] -= tx.amount_ + tx.fee_;
  }
  balances_[tx.recipient_] += tx.amount_;
  return true;
}
bool Ledger::reverse_transaction(const Transaction &tx) {
  if (!balances_.contains(tx.recipient_))
    return false;
  if (tx.amount_ > balances_[tx.recipient_])
    return false;
  balances_[tx.recipient_] -= tx.amount_;
  if (tx.sender_ != kCoinbaseSender) {
    balances_[tx.sender_] += tx.amount_ + tx.fee_;
  }

  return true;
}

std::vector<std::pair<crypto::str, uint64_t>> Ledger::all_balances() const {
std::vector<std::pair<crypto::str, uint64_t>> balances;
balances.reserve(balances_.size());
for(const auto& [address, amount] : balances_) {
    balances.emplace_back(address, amount);
}
return balances;
}
} // namespace forgechain::core
