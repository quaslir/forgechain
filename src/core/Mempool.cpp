#include "core/Mempool.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Signature.hpp"
#include <vector>
#include <cstddef>
#include <algorithm>
namespace forgechain::core {
    bool Mempool::add_transaction(const Transaction& tx, const crypto::bytes& sender_public_key) {
        if(crypto::derive_address(sender_public_key) != tx.sender_) return false;
        if(!crypto::verify(tx.serialize(), tx.signature_, sender_public_key)) return false;
        pending_.push_back(tx);
        return true;
    }
    void Mempool::remove_transaction(const Transaction&tx) {
        std::erase(pending_, tx);
    }

    std::vector<Transaction> Mempool::get_transactions_for_block(size_t limit) const {
        size_t count = std::min(limit, size());
        return std::vector<Transaction>{pending_.begin(), pending_.begin() + static_cast<long>(count)};
    }
     size_t Mempool::size() const {
         return pending_.size();
     }
     bool Mempool::empty() const {
         return pending_.empty();
     }
}
