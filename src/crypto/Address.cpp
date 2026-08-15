#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Hash.hpp"
namespace forgechain::crypto {
str derive_address(const bytes &public_key) {
  HashBytes hash = double_sha_256(public_key);
  return to_hex(hash);
}
} // namespace forgechain::crypto
