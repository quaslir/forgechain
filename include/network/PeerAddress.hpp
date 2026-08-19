#pragma once

#include "crypto/CommonTypes.hpp"
#include <cstdint>
namespace forgechain::network {
struct PeerAddress {
  crypto::str host;
  uint16_t port;
};
} // namespace forgechain::network
