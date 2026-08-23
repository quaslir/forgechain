#pragma once

#include "crypto/CommonTypes.hpp"
#include "network/PeerAddress.hpp"
#include <fstream>
#include <optional>
#include <vector>
namespace forgechain::network {
std::optional<std::vector<PeerAddress>>
load_bootstrap_peers(const crypto::str &path);
void create_bootstrap_file(const crypto::str &path);
} // namespace forgechain::network
