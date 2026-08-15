#include "core/Block.hpp"
#include "crypto/Hash.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstdint>

namespace forgechain::core {
Block::Block(uint32_t version, HashBytes prev_hash, uint64_t timestamp)
    : version_(version), prev_hash_(prev_hash), timestamp_(timestamp),
      difficulty_(0), nonce_(0) {
  merkle_root_ = HashBytes();
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

  return out;
}
} // namespace forgechain::core
