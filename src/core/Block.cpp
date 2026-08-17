#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Hash.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <optional>
#include <algorithm>
namespace forgechain::core {
Block::Block(uint32_t version, HashBytes prev_hash, uint64_t timestamp,
             std::vector<Transaction> transactions)
    : version_(version), prev_hash_(prev_hash), timestamp_(timestamp),
      difficulty_(0), nonce_(0), transactions_(std::move(transactions)) {
  crypto::bytes tx_out;
  for (const auto &tx : transactions_) {
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

  auto tx_count = static_cast<uint32_t>(transactions_.size());
  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&tx_count), reinterpret_cast<const uint8_t*>(&tx_count) + sizeof(tx_count));

  crypto::bytes tx_out;
  for (const auto &tx : transactions_) {
    bytes tx_serialized = tx.serialize();
    auto tx_len = static_cast<uint32_t>(tx_serialized.size());
    tx_out.insert(tx_out.end(), reinterpret_cast<const uint8_t*>(&tx_len), reinterpret_cast<const uint8_t*>(&tx_len) + sizeof(tx_len));
    tx_out.insert(tx_out.end(), tx_serialized.begin(), tx_serialized.end());
  }

  out.insert(out.end(), tx_out.begin(), tx_out.end());

  return out;
}

std::optional<Block> Block::deserialize(const crypto::bytes& payload) {
    constexpr uint32_t HASH_BYTES_SIZE = 32;
    size_t offset = 0;
    if(payload.size() < sizeof(uint32_t) + offset) return std::nullopt;

    uint32_t version = *reinterpret_cast<const uint32_t*>(payload.data());
    offset += sizeof(version);

    if(payload.size() < HASH_BYTES_SIZE + offset) return std::nullopt;
    crypto::HashBytes prev_hash;

    std::copy(payload.data() + offset, payload.data() + offset + HASH_BYTES_SIZE, prev_hash.data());
    offset += HASH_BYTES_SIZE;

    if(payload.size() < HASH_BYTES_SIZE + offset) return std::nullopt;
    crypto::HashBytes merkle_root;

    std::copy(payload.data() + offset, payload.data() + offset + HASH_BYTES_SIZE, merkle_root.data());
    offset += HASH_BYTES_SIZE;

    if(payload.size() < sizeof(uint64_t) + offset) return std::nullopt;
    uint64_t timestamp = *reinterpret_cast<const uint64_t*>(payload.data() + offset);
    offset += sizeof(timestamp);

    if(payload.size() < sizeof(uint32_t) + offset) return std::nullopt;
    uint32_t difficulty = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
    offset += sizeof(difficulty);

    if(payload.size() < sizeof(uint32_t) + offset) return std::nullopt;
    uint32_t nonce = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
    offset += sizeof(nonce);

    if(payload.size() < sizeof(uint32_t) + offset) return std::nullopt;
    uint32_t tx_count = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
    offset += sizeof(tx_count);


        std::vector<Transaction> transactions;

    for(uint32_t i = 0; i < tx_count; i++) {
        if(payload.size() < sizeof(uint32_t) + offset) return std::nullopt;
        uint32_t tx_len = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
        offset += sizeof(tx_len);

        if(payload.size() < offset + tx_len) return std::nullopt;
        crypto::bytes tx_payload(payload.data() + offset, payload.data() + offset + tx_len);
        auto tx_container = Transaction::deserialize(tx_payload);
        if(!tx_container.has_value()) return std::nullopt;
            transactions.push_back(*tx_container);
       offset += tx_len;
    }
    if(offset != payload.size()) return std::nullopt;

    Block block{version, prev_hash, timestamp, transactions};
    block.difficulty_ = difficulty;
    block.nonce_ = nonce;
    block.merkle_root_ = merkle_root;
    block.hash_ = block.compute_hash();
    return block;
}


} // namespace forgechain::core
