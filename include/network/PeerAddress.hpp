#pragma once

#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <optional>
#include <vector>
namespace forgechain::network {
struct PeerAddress {
  crypto::str host;
  uint16_t port;
};
[[nodiscard]] crypto::bytes
serialize_peer_address(const PeerAddress &peer_address);
[[nodiscard]] std::optional<PeerAddress>
deserialize_peer_address(const crypto::bytes &payload);

[[nodiscard]] crypto::bytes
serialize_peer_list(const std::vector<PeerAddress> &addresses);
[[nodiscard]] std::optional<std::vector<PeerAddress>>
deserialize_peer_list(const crypto::bytes &payload);
} // namespace forgechain::network
