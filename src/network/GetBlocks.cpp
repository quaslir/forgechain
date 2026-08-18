#include "network/GetBlocks.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <optional>
namespace forgechain::network {
    crypto::bytes serialize_getblocks(uint64_t from_height) {
        crypto::bytes out;
        out.insert(out.end(), reinterpret_cast<const uint8_t *>(&from_height), reinterpret_cast<const uint8_t*>(&from_height) + sizeof(from_height));
        return out;
    }
    std::optional<uint64_t> deserialize_getblocks(const crypto::bytes& payload) {
        if(payload.size() != sizeof(uint64_t)) return std::nullopt;
        uint64_t from_height = *reinterpret_cast<const uint64_t*>(payload.data());
        return from_height;
    }
}
