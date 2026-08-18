#include "core/Mempool.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Signature.hpp"
#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>
namespace forgechain::core {
bool Mempool::add_transaction(const Transaction &tx,
                              const crypto::bytes &sender_public_key) {
  if (crypto::derive_address(sender_public_key) != tx.sender_)
    return false;
  if (!crypto::verify(tx.serialize_for_signing(), tx.signature_,
                      sender_public_key))
    return false;
  pending_.push_back(tx);
  return true;
}
void Mempool::remove_transaction(const Transaction &tx) {
  std::erase(pending_, tx);
}

std::vector<Transaction>
Mempool::get_transactions_for_block(size_t limit) const {
  size_t count = std::min(limit, size());
  return std::vector<Transaction>{pending_.begin(),
                                  pending_.begin() + static_cast<long>(count)};
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
