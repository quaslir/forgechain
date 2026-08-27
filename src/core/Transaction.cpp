#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Hash.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sys/types.h>
#include <utility>
namespace forgechain::core {
Transaction::Transaction(str sender, str recipient, uint64_t amount,
                         core::bytes sender_public_key, uint64_t fee)
    : sender_(std::move(sender)), recipient_(std::move(recipient)),
      sender_public_key_(std::move(sender_public_key)), amount_(amount), fee_(fee) {}
crypto::bytes Transaction::serialize_for_signing() const {

  crypto::bytes out;
  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&amount_),
             reinterpret_cast<const uint8_t *>(&amount_) + sizeof(amount_));
  auto sender_len = static_cast<uint32_t>(sender_.size());
  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&sender_len),
             reinterpret_cast<const uint8_t *>(&sender_len) +
                 sizeof(sender_len));
  out.insert(out.end(), sender_.begin(), sender_.end());
  auto recipient_len = static_cast<uint32_t>(recipient_.size());
  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&recipient_len),
             reinterpret_cast<const uint8_t *>(&recipient_len) +
                 sizeof(recipient_len));
  out.insert(out.end(), recipient_.begin(), recipient_.end());
  auto sender_public_key_len = static_cast<uint32_t>(sender_public_key_.size());
  out.insert(out.end(),
             reinterpret_cast<const uint8_t *>(&sender_public_key_len),
             reinterpret_cast<const uint8_t *>(&sender_public_key_len) +
                 sizeof(sender_public_key_len));
  out.insert(out.end(), sender_public_key_.begin(), sender_public_key_.end());
  out.insert(out.end(), reinterpret_cast<const uint8_t*>(&fee_),
      reinterpret_cast<const uint8_t*>(&fee_) + sizeof(fee_));
  return out;
}
crypto::bytes Transaction::serialize() const {
  crypto::bytes payload_without_signature = serialize_for_signing();
  auto signature_len = static_cast<uint32_t>(signature_.size());
  payload_without_signature.insert(
      payload_without_signature.end(),
      reinterpret_cast<const uint8_t *>(&signature_len),
      reinterpret_cast<const uint8_t *>(&signature_len) +
          sizeof(signature_len));
  payload_without_signature.insert(payload_without_signature.end(),
                                   signature_.begin(), signature_.end());

  return payload_without_signature;
}
std::optional<Transaction>
Transaction::deserialize(const crypto::bytes &payload) {
  size_t offset = 0;
  if (payload.size() < offset + sizeof(uint64_t))
    return std::nullopt;
  uint64_t amount =
      *reinterpret_cast<const uint64_t *>(payload.data() + offset);
  offset += sizeof(amount);

  if (payload.size() < offset + sizeof(uint32_t))
    return std::nullopt;
  uint32_t sender_len =
      *reinterpret_cast<const uint32_t *>(payload.data() + offset);
  offset += sizeof(sender_len);

  if (payload.size() < offset + sender_len)
    return std::nullopt;
  str sender;
  sender.resize(sender_len);
  std::copy(payload.data() + offset, payload.data() + offset + sender_len,
            sender.data());
  offset += sender_len;

  if (payload.size() < offset + sizeof(uint32_t))
    return std::nullopt;
  uint32_t recipient_len =
      *reinterpret_cast<const uint32_t *>(payload.data() + offset);
  offset += sizeof(recipient_len);

  if (payload.size() < offset + recipient_len)
    return std::nullopt;
  str recipient;
  recipient.resize(recipient_len);
  std::copy(payload.data() + offset, payload.data() + offset + recipient_len,
            recipient.data());
  offset += recipient_len;

  if (payload.size() < offset + sizeof(uint32_t))
    return std::nullopt;
  uint32_t sender_public_key_len =
      *reinterpret_cast<const uint32_t *>(payload.data() + offset);
  offset += sizeof(sender_public_key_len);

  if (payload.size() < offset + sender_public_key_len)
    return std::nullopt;
  core::bytes sender_public_key(sender_public_key_len);
  std::copy(payload.data() + offset,
            payload.data() + offset + sender_public_key_len,
            sender_public_key.data());
  offset += sender_public_key_len;
  if(payload.size() < offset + sizeof(uint64_t)) return std::nullopt;
  auto fee = *reinterpret_cast<const uint64_t*>(payload.data() + offset);
  offset += sizeof(uint64_t);



  if (payload.size() < offset + sizeof(uint32_t))
    return std::nullopt;
  uint32_t signature_len =
      *reinterpret_cast<const uint32_t *>(payload.data() + offset);
  offset += sizeof(signature_len);

  if (payload.size() < offset + signature_len)
    return std::nullopt;
  core::bytes signature(signature_len);
  std::copy(payload.data() + offset, payload.data() + offset + signature_len,
            signature.data());
  offset += signature_len;

  if (payload.size() != offset)
    return std::nullopt;

  Transaction tx{sender, recipient, amount, std::move(sender_public_key), fee};
  tx.signature_ = std::move(signature);
  return tx;
}
crypto::HashBytes Transaction::compute_hash() const {
  return crypto::double_sha_256(serialize_for_signing());
}
bool Transaction::operator==(const Transaction &tx) const {
  return sender_ == tx.sender_ && recipient_ == tx.recipient_ &&
         amount_ == tx.amount_ && signature_ == tx.signature_ && fee_ == tx.fee_;
}

} // namespace forgechain::core
