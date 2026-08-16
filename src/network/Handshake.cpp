#pragma once

#include "crypto/CommonTypes.hpp"
#include "network/Message.hpp"
#include "network/Handshake.hpp"
#include <cstdint>
#include <optional>
#include <cstddef>
namespace forgechain::network {

    crypto::bytes serialize_version(const VersionInfo& info) {
        crypto::bytes out;
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&info.protocol_version),
            reinterpret_cast<const uint8_t*>(&info.protocol_version) + sizeof(info.protocol_version));

        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&info.chain_height),
            reinterpret_cast<const uint8_t*>(&info.chain_height) + sizeof(info.chain_height));

        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&info.timestamp),
            reinterpret_cast<const uint8_t*>(&info.timestamp) + sizeof(info.timestamp));

        return out;
    }
    VersionInfo deserialize_version(const crypto::bytes& payload) {
        VersionInfo info;
        info.protocol_version = *reinterpret_cast<const uint32_t*>(payload.data());
        info.chain_height = *reinterpret_cast<const uint64_t*>(payload.data() + sizeof(info.protocol_version));
        info.timestamp = *reinterpret_cast<const uint64_t*>(payload.data() + sizeof(info.protocol_version) + sizeof(info.chain_height));

        return info;
    }
    std::optional<VersionInfo> perform_handshake(int fd, const VersionInfo& info) {
        Message msg{.type = MessageType::VERSION, .payload = serialize_version(info)};
        if(!send_message(fd, msg)) return std::nullopt;
        Message incoming_msg;
        if(!receive_message(fd, incoming_msg)) return std::nullopt;
        if(incoming_msg.type != MessageType::VERSION) return std::nullopt;
        constexpr size_t kExpectedVersionPayloadSize =
                sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t);

        if(incoming_msg.payload.size() != kExpectedVersionPayloadSize) return std::nullopt;
        return deserialize_version(incoming_msg.payload);
    }
}
