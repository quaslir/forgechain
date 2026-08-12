#include <cstdint>
#include <vector>
#include "crypto/Hash.hpp"
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
    void serialize();

    private:
    HashBytes compute_hash();
};
