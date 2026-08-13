#pragma once
#include <cstdint>
#include <vector>

#include "crypto/Hash.hpp"

namespace forgechain::core {

using forgechain::crypto::HashBytes;
class Block {
    public:
    uint32_t version_;
    HashBytes prev_hash_;
    HashBytes merkle_root_;
    uint64_t timestamp_;
    uint32_t difficulty_;
    uint32_t nonce_;

    //std::vector<Transaction> transactions_;
    HashBytes hash_;

    Block(uint32_t version, HashBytes prev_hash, uint64_t timestamp);

        [[nodiscard]] HashBytes compute_hash() const;
    private:
            [[nodiscard]] std::vector<uint8_t> serialize() const;
};
}
