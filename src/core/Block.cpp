#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/Hash.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <utility>
#include <vector>
namespace forgechain::core {
Block::Block(uint32_t version, HashBytes prev_hash, uint64_t timestamp, std::vector<Transaction> transactions)
    : version_(version), prev_hash_(prev_hash), timestamp_(timestamp),
      difficulty_(0), nonce_(0), transactions_(std::move(transactions)) {
          crypto::bytes tx_out;
          for(const auto& tx : transactions_) {
              bytes tx_serialized = tx.serialize();
              tx_out.insert(tx_out.end(), tx_serialized.begin(), tx_serialized.end());
          }

  merkle_root_ = crypto::double_sha_256(tx_out);
  hash_ = compute_hash();
}

HashBytes Block::compute_hash() const {
  return forgechain::crypto::double_sha_256(serialize());
}

crypto::bytes Block::serialize() const {
  crypto::bytes out;
  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&version_),
             reinterpret_cast<const uint8_t *>(&version_) + sizeof(version_));

  out.insert(out.end(), prev_hash_.begin(), prev_hash_.end());

  out.insert(out.end(), merkle_root_.begin(), merkle_root_.end());

  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&timestamp_),
             reinterpret_cast<const uint8_t *>(&timestamp_) +
                 sizeof(timestamp_));

  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&difficulty_),
             reinterpret_cast<const uint8_t *>(&difficulty_) +
                 sizeof(difficulty_));

  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&nonce_),
             reinterpret_cast<const uint8_t *>(&nonce_) + sizeof(nonce_));


  crypto::bytes tx_out;
  for(const auto& tx : transactions_) {
      bytes tx_serialized = tx.serialize();
      tx_out.insert(tx_out.end(), tx_serialized.begin(), tx_serialized.end());
  }

  out.insert(out.end(), tx_out.begin(), tx_out.end());


  return out;
}
} // namespace forgechain::core
