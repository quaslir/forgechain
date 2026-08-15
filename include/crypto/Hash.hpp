#pragma once
#include "crypto/CommonTypes.hpp"
#include <openssl/evp.h>
namespace forgechain::crypto {

HashBytes sha256(const bytes &data);
HashBytes double_sha_256(const bytes &data);
str to_hex(const HashBytes &data);
} // namespace forgechain::crypto
