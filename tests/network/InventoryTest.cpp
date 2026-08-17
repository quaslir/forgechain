#include "network/Inventory.hpp"
#include "crypto/CommonTypes.hpp"
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

using namespace forgechain;
namespace {

crypto::HashBytes make_hash(uint8_t fill_byte) {
    crypto::HashBytes hash{};
    hash.fill(fill_byte);
    return hash;
}

}  // namespace

TEST(InventorySerialization, RoundTripPreservesSingleItem) {
    std::vector<network::InventoryItem> items;
    items.push_back({.type = network::InventoryItemType::TRANSACTION,.hash =  make_hash(0)});
   crypto::bytes payload =  network::serialize_inventory(items);
   auto items_container =  network::deserialize_inventory(payload);
   ASSERT_TRUE(items_container.has_value());
   ASSERT_EQ(items, *items_container);
}

TEST(InventorySerialization, RoundTripPreservesMultipleItems) {
    std::vector<network::InventoryItem> items;
    items.push_back({.type = network::InventoryItemType::BLOCK, .hash = make_hash(1)});
    items.push_back({.type = network::InventoryItemType::TRANSACTION, .hash = make_hash(2)});
    items.push_back({.type = network::InventoryItemType::BLOCK, .hash = make_hash(3)});

    crypto::bytes payload = network::serialize_inventory(items);
    auto items_container = network::deserialize_inventory(payload);

    ASSERT_TRUE(items_container.has_value());
    ASSERT_EQ(items, *items_container);
}

TEST(InventorySerialization, EmptyListRoundTrips) {
    std::vector<network::InventoryItem> items;

    crypto::bytes payload = network::serialize_inventory(items);
    ASSERT_EQ(payload.size(), sizeof(uint32_t));

    auto items_container = network::deserialize_inventory(payload);
    ASSERT_TRUE(items_container.has_value());
    EXPECT_TRUE(items_container->empty());
}

TEST(InventoryDeserialization, TruncatedCountFieldIsRejected) {
    crypto::bytes payload{0x01, 0x02};

    auto result = network::deserialize_inventory(payload);
    EXPECT_FALSE(result.has_value());
}

TEST(InventoryDeserialization, MismatchedDeclaredCountIsRejected) {
    std::vector<network::InventoryItem> items;
    items.push_back({.type = network::InventoryItemType::BLOCK, .hash = make_hash(9)});
    items.push_back({.type = network::InventoryItemType::TRANSACTION, .hash = make_hash(8)});

    crypto::bytes payload = network::serialize_inventory(items);
    payload.resize(payload.size() - 10);

    auto result = network::deserialize_inventory(payload);
    EXPECT_FALSE(result.has_value());
}

TEST(InventoryDeserialization, InvalidItemTypeIsRejected) {
    crypto::bytes payload;
    uint32_t count = 1;
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&count),
                    reinterpret_cast<const uint8_t*>(&count) + sizeof(count));
    payload.push_back(0xFF);
    crypto::HashBytes hash = make_hash(5);
    payload.insert(payload.end(), hash.begin(), hash.end());

    auto result = network::deserialize_inventory(payload);
    EXPECT_FALSE(result.has_value());
}
