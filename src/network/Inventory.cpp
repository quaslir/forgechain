#include "network/Inventory.hpp"
#include "crypto/CommonTypes.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace forgechain::network {
crypto::bytes serialize_inventory(const std::vector<InventoryItem> &items) {
  crypto::bytes out;
  auto count = static_cast<uint32_t>(items.size());
  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&count),
             reinterpret_cast<const uint8_t *>(&count) + sizeof(count));
  for (const auto &item : items) {
    out.insert(out.end(), reinterpret_cast<const uint8_t *>(&item.type),
               reinterpret_cast<const uint8_t *>(&item.type) +
                   sizeof(item.type));
    out.insert(out.end(), item.hash.begin(), item.hash.end());
  }

  return out;
}
std::optional<std::vector<InventoryItem>>
deserialize_inventory(const crypto::bytes &payload) {
  if (payload.size() < sizeof(uint32_t))
    return std::nullopt;
  uint32_t count = *reinterpret_cast<const uint32_t *>(payload.data());
  size_t expected_size =
      sizeof(uint32_t) + static_cast<size_t>(count) * kInventoryItemSize;
  if (payload.size() != expected_size)
    return std::nullopt;
  std::vector<InventoryItem> items;
  items.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    size_t offset = sizeof(uint32_t) + i * kInventoryItemSize;
    uint8_t type = payload[offset];
    if (type != 0 && type != 1)
      return std::nullopt;
    crypto::HashBytes hash;
    std::copy(payload.begin() + static_cast<long>(offset) + 1,
              payload.begin() + static_cast<long>(offset) + kInventoryItemSize,
              hash.begin());
    items.push_back(InventoryItem{.type = static_cast<InventoryItemType>(type),
                                  .hash = hash});
  }

  return items;
}
} // namespace forgechain::network
