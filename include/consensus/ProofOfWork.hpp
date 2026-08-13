#pragma once

#include "crypto/Hash.hpp"
#include <cstdint>

namespace forgechain::consensus {
    using forgechain::crypto::HashBytes;
    bool meets_target(const HashBytes& hash, uint32_t difficulty);
}
