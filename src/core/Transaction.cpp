#include "core/Transaction.hpp"
#include "crypto/Hash.hpp"
#include <cstdint>
#include <utility>
namespace forgechain::core {
    Transaction::Transaction(str sender, str recipient, uint64_t amount) : sender_(std::move(sender)), recipient_(std::move(recipient)), amount_(amount) {}
    crypto::bytes Transaction::serialize() const {
        crypto::bytes out;
         out.insert(out.end(), reinterpret_cast<const uint8_t *>(&amount_),
             reinterpret_cast<const uint8_t *>(&amount_) + sizeof(amount_));

         out.insert(out.end(), sender_.begin(), sender_.end());

         out.insert(out.end(), recipient_.begin(), recipient_.end());

         return out;
    }
}
