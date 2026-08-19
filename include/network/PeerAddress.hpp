#pragma once

#include <cstdint>
#include "crypto/CommonTypes.hpp"
namespace forgechain::network {
    struct PeerAddress {
        crypto::str host;
        uint16_t port;
    };
}
