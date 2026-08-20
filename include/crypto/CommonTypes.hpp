#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace forgechain::crypto {

using HashBytes = std::array<uint8_t, 32>;
using bytes = std::vector<uint8_t>;
using str = std::string;

struct HashBytesHasher {
  std::size_t operator()(const crypto::HashBytes &hash) const {
    size_t result;
    std::memcpy(&result, hash.data(), sizeof(result));
    return result;
  }
};
} // namespace forgechain::crypto
