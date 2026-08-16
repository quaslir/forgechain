#pragma once

#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <optional>
namespace forgechain::network {
    struct VersionInfo {
        uint32_t protocol_version;
        uint64_t chain_height;
        uint64_t timestamp;
    };

    crypto::bytes serialize_version(const VersionInfo& info);
    VersionInfo deserialize_version(const crypto::bytes& payload);
    std::optional<VersionInfo> perform_handshake(int fd, const VersionInfo& info);
}
