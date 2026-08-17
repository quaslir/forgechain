#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Hash.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <optional>
namespace forgechain::core {
Transaction::Transaction(str sender, str recipient, uint64_t amount)
    : sender_(std::move(sender)), recipient_(std::move(recipient)),
      amount_(amount) {}
crypto::bytes Transaction::serialize() const {
  crypto::bytes out;
  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&amount_),
             reinterpret_cast<const uint8_t *>(&amount_) + sizeof(amount_));
  auto sender_len =static_cast<uint32_t>(sender_.size());
  out.insert(out.end(),reinterpret_cast<const uint8_t*>(&sender_len) , reinterpret_cast<const uint8_t*>(&sender_len) + sizeof(sender_len));
  out.insert(out.end(), sender_.begin(), sender_.end());
  auto recipient_len = static_cast<uint32_t>(recipient_.size());
  out.insert(out.end(),reinterpret_cast<const uint8_t*>(&recipient_len) , reinterpret_cast<const uint8_t*>(&recipient_len) + sizeof(recipient_len));
  out.insert(out.end(), recipient_.begin(), recipient_.end());

  return out;
}
std::optional<Transaction> Transaction::deserialize(const crypto::bytes& payload) {
    size_t offset = 0;
    if(payload.size() < offset + sizeof(uint64_t)) return std::nullopt;
    uint64_t amount_ = *reinterpret_cast<const uint64_t*>(payload.data() + offset);
    offset += sizeof(amount_);

if(payload.size() < offset + sizeof(uint32_t)) return std::nullopt;
uint32_t sender_len = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
offset += sizeof(sender_len);


if(payload.size() < offset + sender_len) return std::nullopt;
str sender;
sender.resize(sender_len);
std::copy(payload.data() + offset, payload.data() + offset + sender_len, sender.data());
offset += sender_len;


if(payload.size() < offset + sizeof(uint32_t)) return std::nullopt;
uint32_t recipient_len = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
offset += sizeof(recipient_len);

if(payload.size() < offset + recipient_len) return std::nullopt;
str recipient;
recipient.resize(recipient_len);
std::copy(payload.data() + offset, payload.data() + offset + recipient_len, recipient.data());
offset += recipient_len;

if(payload.size() != offset) return std::nullopt;
return Transaction{sender, recipient, amount_};
}
crypto::HashBytes Transaction::compute_hash() const {
  return crypto::double_sha_256(serialize());
}
bool Transaction::operator==(const Transaction &tx) {
  return sender_ == tx.sender_ && recipient_ == tx.recipient_ &&
         amount_ == tx.amount_ && signature_ == tx.signature_;
}

} // namespace forgechain::core
