#pragma once
#include "crypto/CommonTypes.hpp"
namespace forgechain::crypto {
    crypto::bytes sign(const crypto::bytes& message, const crypto::bytes& private_key);
}
