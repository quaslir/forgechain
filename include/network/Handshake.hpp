#pragma once

#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
namespace forgechain::network {
struct VersionInfo {
  uint32_t protocol_version;
  uint64_t chain_height;
  uint64_t timestamp;
  uint16_t listen_port;
};
constexpr size_t kExpectedVersionPayloadSize =
    sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint16_t);
crypto::bytes serialize_version(const VersionInfo &info);
VersionInfo deserialize_version(const crypto::bytes &payload);
std::optional<VersionInfo> perform_handshake(int fd, const VersionInfo &info);
} // namespace forgechain::network
