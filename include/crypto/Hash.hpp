#pragma once
#include <openssl/evp.h>
#include "crypto/CommonTypes.hpp"
namespace forgechain::crypto {

HashBytes sha256(const bytes& data);
HashBytes double_sha_256(const bytes& data);
str to_hex(const HashBytes& data);
}
