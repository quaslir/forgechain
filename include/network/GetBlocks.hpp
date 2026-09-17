#pragma once
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <optional>
#include <vector>
namespace forgechain::network {
constexpr auto kMaxLocatorHashes = 1028;
crypto::bytes
serialize_getblocks(const std::vector<crypto::HashBytes> &locator);
std::optional<std::vector<crypto::HashBytes>>
deserialize_getblocks(const crypto::bytes &payload);
} // namespace forgechain::network
