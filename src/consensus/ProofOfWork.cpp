#include "consensus/ProofOfWork.hpp"
#include "crypto/Hash.hpp"
#include <cstddef>
#include <cstdint>
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
}
