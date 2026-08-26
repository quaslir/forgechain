#include "network/PeerAddress.hpp"
#include "crypto/CommonTypes.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
namespace forgechain::network {
   crypto::bytes serialize_peer_address(const PeerAddress& peer_address) {
       crypto::bytes out;
       out.insert(out.end(), reinterpret_cast<const uint8_t*>(&peer_address.port),
           reinterpret_cast<const uint8_t*>(&peer_address.port) + sizeof(peer_address.port));
       auto length = static_cast<uint32_t>(peer_address.host.size());
       out.insert(out.end(), reinterpret_cast<const uint8_t*>(&length),
           reinterpret_cast<const uint8_t*>(&length) + sizeof(length));
       out.insert(out.end(), peer_address.host.begin(), peer_address.host.end());
       return out;
   }
     std::optional<PeerAddress> deserialize_peer_address(const crypto::bytes& payload) {
         if(payload.size() < sizeof(uint16_t)) return std::nullopt;
         size_t offset = 0;
        auto port = *reinterpret_cast<const uint16_t*>(payload.data());
        offset += sizeof(port);

        if(payload.size() < offset + sizeof(uint32_t)) return std::nullopt;
        auto length = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
        offset += sizeof(length);

        if(payload.size() < offset + length) return std::nullopt;
        crypto::str host;
        host.resize(length);

        std::copy(payload.data() + offset, payload.data() + offset + length, host.data());
        offset += length;

        if(offset != payload.size()) return std::nullopt;

        return PeerAddress{.host = std::move(host), .port = port};
     }


     crypto::bytes serialize_peer_list(const std::vector<PeerAddress>& addresses) {
         crypto::bytes out;
         auto count = static_cast<uint32_t>(addresses.size());
         out.insert(out.end(), reinterpret_cast<const uint8_t*>(&count),
             reinterpret_cast<const uint8_t*>(&count) + sizeof(count));

         for(const auto& address : addresses) {
             auto encoded = serialize_peer_address(address);
             auto length = static_cast<uint32_t>(encoded.size());
             out.insert(out.end(), reinterpret_cast<const uint8_t*>(&length),
                 reinterpret_cast<const uint8_t*>(&length) + sizeof(length));
             out.insert(out.end(), encoded.begin(), encoded.end());
         }
         return out;
     }
     std::optional<std::vector<PeerAddress>> deserialize_peer_list(const crypto::bytes& payload) {
         if(payload.size() < sizeof(uint32_t)) return std::nullopt;
         size_t offset = 0;
         auto count = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
         offset += sizeof(count);

         std::vector<PeerAddress> addresses;
         addresses.reserve(count);

         for(uint32_t i = 0; i < count; i++) {
             if(payload.size() < offset + sizeof(uint32_t)) return std::nullopt;
             auto length = *reinterpret_cast<const uint32_t*>(payload.data() + offset);
             offset += sizeof(length);

             if(payload.size() < offset + length) return std::nullopt;
             crypto::bytes entry(payload.begin() + static_cast<long>(offset), payload.begin() + static_cast<long>(offset)
                 + length);
             offset += length;

             auto peer_address = deserialize_peer_address(entry);
             if(!peer_address.has_value()) return std::nullopt;
             addresses.push_back(std::move(*peer_address));
         }

         if(payload.size() != offset) return std::nullopt;
         return addresses;
     }
}
