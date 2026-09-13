#include "core/Mempool.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Signature.hpp"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <vector>
namespace forgechain::core {

Mempool::Mempool(size_t max_size) : max_size_(max_size) {}

bool Mempool::add_transaction(const Transaction &tx) {
if(!has_valid_signature(tx)) return false;
  if (has_transaction(tx.compute_hash()))
    return false;

  // eviction

  if (pending_.size() >= max_size_) {
    auto cheapest_tx = pending_.rbegin();
    if (tx.fee_ > cheapest_tx->fee_) {
      auto it = std::prev(pending_.end());
      pending_.erase(it);
    } else
      return false;
  }
  pending_.insert(tx);
  return true;
}
void Mempool::remove_transaction(const Transaction &tx) {
  auto it = std::find_if(
      pending_.begin(), pending_.end(), [&tx](const Transaction &transaction) {
        return tx.compute_hash() == transaction.compute_hash();
      });

  if (it != pending_.end()) {
    pending_.erase(it);
  }
}

std::vector<Transaction>
Mempool::get_transactions_for_block(size_t limit) const {
  size_t count = std::min(limit, size());
  return std::vector<Transaction>{
      pending_.begin(), std::next(pending_.begin(), static_cast<long>(count))};
}
size_t Mempool::size() const { return pending_.size(); }
bool Mempool::empty() const { return pending_.empty(); }
bool Mempool::has_transaction(const crypto::HashBytes &hash) const {
  auto it = std::find_if(
      pending_.begin(), pending_.end(),
      [&hash](const Transaction &tx) { return tx.compute_hash() == hash; });

  return it != pending_.end();
}

[[nodiscard]] std::optional<Transaction>
Mempool::find(const crypto::HashBytes &hash) const {
  for (const auto &tx : pending_) {
    if (tx.compute_hash() == hash) {
      return tx;
    }
  }

  return std::nullopt;
}

} // namespace forgechain::core
