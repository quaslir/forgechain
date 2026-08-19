#pragma once

#include "network/PeerAddress.hpp"
#include "crypto/CommonTypes.hpp"
#include <vector>
#include <optional>
namespace forgechain::network {
    std::optional<std::vector<PeerAddress>> load_bootstrap_peers(const crypto::str& path);
}
