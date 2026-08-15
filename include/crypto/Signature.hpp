#pragma once
#include "crypto/CommonTypes.hpp"
namespace forgechain::crypto {
crypto::bytes sign(const crypto::bytes &message,
                   const crypto::bytes &private_key);
bool verify(const crypto::bytes &message, const crypto::bytes &signature,
            const crypto::bytes &public_key);
} // namespace forgechain::crypto
