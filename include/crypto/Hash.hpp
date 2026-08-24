#pragma once
#include "crypto/CommonTypes.hpp"
#include <openssl/evp.h>
namespace forgechain::crypto {

HashBytes sha256(const bytes &data);
HashBytes double_sha_256(const bytes &data);
str to_hex(const HashBytes &data);
str to_hex(const HashBytes &data);
str to_hex(const crypto::bytes &data);
std::optional<crypto::bytes> from_hex(const str &hex);
std::optional<uint8_t> hex_digit_value(char c);
} // namespace forgechain::crypto
