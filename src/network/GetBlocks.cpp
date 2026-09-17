#include "network/GetBlocks.hpp"
#include "crypto/CommonTypes.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace forgechain::network {

crypto::bytes serialize_getblocks(const std::vector<crypto::HashBytes> &locator) {
    crypto::bytes payload;
    auto count = static_cast<uint32_t>(locator.size());
    payload.reserve((static_cast<size_t>(count) * 32) + sizeof(uint32_t));
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&count),
        reinterpret_cast<const uint8_t*>(&count) + sizeof(count));

   for(const auto&hash : locator) {
       payload.insert(payload.end(), hash.begin(), hash.end());
   }

    return payload;
}
std::optional<std::vector<crypto::HashBytes>>
deserialize_getblocks(const crypto::bytes &payload) {
    size_t offset = 0;
    if(payload.size()  < offset + sizeof(uint32_t)) return std::nullopt;
    auto count = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
    if(count> kMaxLocatorHashes) return std::nullopt;
    offset +=sizeof(uint32_t);
    if(payload.size() != static_cast<size_t>(count) * 32 + sizeof(uint32_t)) return std::nullopt;

    std::vector<crypto::HashBytes> hashes;
    hashes.reserve(static_cast<size_t>(count));

    for(size_t i = 0; i < count; i++) {
        crypto::HashBytes hash;
        std::copy(payload.data() + offset, payload.data() + offset + 32, hash.data());
        hashes.push_back(hash);
        offset += 32;
    }
    return hashes;
}
} // namespace forgechain::network
