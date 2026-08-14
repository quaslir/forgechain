#pragma once
#include "crypto/Hash.hpp"
#include <cstdint>
#include <string>
namespace forgechain::core {
    using forgechain::crypto::str;
    using forgechain::crypto::bytes;


    struct Transaction {
        str sender_;
        str recipient_;
        uint64_t amount_;
        bytes signature_;

        Transaction(str sender, str recipient, uint64_t amount);
        [[nodiscard]] bytes serialize() const;
    };
}
