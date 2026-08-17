#pragma once

#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace forgechain::network {
enum class InventoryItemType : uint8_t { BLOCK = 0, TRANSACTION = 1 };

struct InventoryItem {
  InventoryItemType type;
  crypto::HashBytes hash;

  bool operator==(const InventoryItem &) const = default;
};

constexpr size_t kInventoryItemSize =
    sizeof(uint8_t) + sizeof(crypto::HashBytes);

crypto::bytes serialize_inventory(const std::vector<InventoryItem> &items);
std::optional<std::vector<InventoryItem>>
deserialize_inventory(const crypto::bytes &payload);
} // namespace forgechain::network
