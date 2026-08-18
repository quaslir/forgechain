#pragma once
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <optional>
namespace forgechain::network {
crypto::bytes serialize_getblocks(uint64_t from_height);
std::optional<uint64_t> deserialize_getblocks(const crypto::bytes &payload);
} // namespace forgechain::network
