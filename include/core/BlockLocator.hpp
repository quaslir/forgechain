#pragma once
#include "core/Blockchain.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <optional>
#include <vector>
namespace forgechain::core {
[[nodiscard]] std::vector<crypto::HashBytes>
build_locator(const Blockchain &chain);

[[nodiscard]] std::optional<size_t>
find_locator_match(const Blockchain &chain,
                   const std::vector<crypto::HashBytes> &locator);
} // namespace forgechain::core
