#pragma once
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <unordered_map>
#include <optional>
namespace forgechain::core {
    class Ledger {
        public:
            void set_balance(const crypto::str& address, uint64_t amount);
            [[nodiscard]] std::optional<uint64_t> get_balance(const crypto::str& address) const;

            bool apply_transaction(const Transaction& tx);

        private:
            std::unordered_map<str, uint64_t> balances_;
    };
}
