#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "crypto/Hash.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <limits>
namespace forgechain::consensus {
    using forgechain::crypto::HashBytes;
    bool meets_target(const HashBytes& hash, uint32_t difficulty) {
                uint32_t result = 0;
        for(const auto& byte : hash) {

        uint8_t mask = 0x80;
        bool found_one = false;
        for(int i = 0; i < 8; i++) {
            if(!(byte & mask))  {
                result++;
                mask >>= 1;
            } else {
                found_one = true;
                break;
            }
        }
        if(found_one) {
            break;
        }
        }

        return result >= difficulty;
    }

    Block mine_block(uint32_t version, HashBytes prev_hash, uint64_t timestamp, uint32_t difficulty) {
        uint32_t nonce = 0;
         constexpr uint32_t kMaxNonce = std::numeric_limits<uint32_t>::max();
        Block block(version, prev_hash, timestamp);
        block.difficulty_ = difficulty;
        while(nonce < kMaxNonce) {
            block.nonce_ = nonce;
            auto hash = block.compute_hash();
        if(meets_target(hash, difficulty)) {
            block.hash_ = hash;
            return block;
        }
        nonce++;
    }
          throw std::runtime_error("mine_block: exhausted nonce range without finding a valid hash");
    }
}
