#pragma once
#include "crypto/CommonTypes.hpp"

namespace forgechain::crypto {
    str derive_address(const bytes& public_key);
}
