#include "crypto/Hash.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/OpenSSLTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <openssl/evp.h>

#include <optional>
#include <stdexcept>
namespace forgechain::crypto {
HashBytes sha256(const bytes &data) {
  HashBytes hash;
  unique_evp_md_ctx ctx(EVP_MD_CTX_new());
  if (!ctx) {
    throw std::runtime_error("EVP_MD_CTX_new failed");
  }

  unsigned int out_len = 0;

  if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx.get(), data.data(), data.size()) != 1 ||
      EVP_DigestFinal_ex(ctx.get(), hash.data(), &out_len) != 1) {
    throw std::runtime_error("SHA-256 computation failed");
  }

  return hash;
}
HashBytes double_sha_256(const bytes &data) {
  HashBytes first = sha256(data);
  bytes first_hash(first.begin(), first.end());
  return sha256(first_hash);
}

str to_hex(const HashBytes &data) {
  static const char *hex_chars = "0123456789abcdef";

  str result;
  result.reserve(data.size() * 2);

  for (uint8_t byte : data) {
    result += hex_chars[(byte >> 4) & 0x0F];
    result += hex_chars[byte & 0x0F];
  }

  return result;
}

str to_hex(const crypto::bytes &data) {
  static const char *hex_chars = "0123456789abcdef";

  str result;
  result.reserve(data.size() * 2);

  for (uint8_t byte : data) {
    result += hex_chars[(byte >> 4) & 0x0F];
    result += hex_chars[byte & 0x0F];
  }

  return result;
}

std::optional<crypto::bytes> from_hex(const str &hex) {
  if (hex.size() % 2 != 0) {
    return std::nullopt;
  }
  crypto::bytes result;
  result.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    auto high = hex_digit_value(hex[i]);
    auto low = hex_digit_value(hex[i + 1]);
    if (!high.has_value() || !low.has_value()) {
      return std::nullopt;
    }

    result.push_back(static_cast<uint8_t>((*high << 4) | *low));
  }
  return result;
}

std::optional<uint8_t> hex_digit_value(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<uint8_t>(c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<uint8_t>(c - 'A' + 10);
  }
  return std::nullopt;
}
} // namespace forgechain::crypto
