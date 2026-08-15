#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace forgechain::crypto {

using HashBytes = std::array<uint8_t, 32>;
using bytes = std::vector<uint8_t>;
using str = std::string;
} // namespace forgechain::crypto
