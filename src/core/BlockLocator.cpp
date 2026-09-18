#include "core/BlockLocator.hpp"
#include "core/Blockchain.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <optional>
#include <vector>
namespace forgechain::core {
std::vector<crypto::HashBytes> build_locator(const Blockchain &chain) {
  if (chain.size() == 0)
    return {};
  std::vector<crypto::HashBytes> hashes;
  size_t step = 1;
  size_t height = chain.size() - 1;

  while (height > 0) {
    hashes.push_back(chain.at(height).hash_);

    if (hashes.size() >= 10) {
      step *= 2;
    }
    if (step > height)
      break;
    height -= step;
  }
  hashes.push_back(chain.at(0).hash_); // add Genesis block
  return hashes;
}

std::optional<size_t>
find_locator_match(const Blockchain &chain,
                   const std::vector<crypto::HashBytes> &locator) {
  for (const auto &hash : locator) {
    auto index = chain.find_height(hash);
    if (index.has_value())
      return index;
  }

  return std::nullopt;
}
} // namespace forgechain::core
