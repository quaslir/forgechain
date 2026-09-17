// GETBLOCKS carries a block locator: a list of hashes the sender has, newest
// first. Its payload comes from peers, so every length in it is untrusted.

#include "network/GetBlocks.hpp"

#include "crypto/CommonTypes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>
#include <cstddef>
#include <optional>
using namespace forgechain::network;
using forgechain::crypto::HashBytes;

namespace {

HashBytes hash_of(uint8_t seed) {
  HashBytes hash{};
  hash.fill(seed);
  hash[0] = seed;
  hash[31] = static_cast<uint8_t>(seed + 1);
  return hash;
}

std::vector<HashBytes> locator_of(size_t count) {
  std::vector<HashBytes> locator;
  for (size_t i = 0; i < count; i++) {
    locator.push_back(hash_of(static_cast<uint8_t>(i + 1)));
  }
  return locator;
}

forgechain::crypto::bytes payload_with_count(uint32_t count,
                                             size_t hash_bytes) {
  forgechain::crypto::bytes payload;
  payload.insert(payload.end(), reinterpret_cast<const uint8_t *>(&count),
                 reinterpret_cast<const uint8_t *>(&count) + sizeof(count));
  payload.resize(payload.size() + hash_bytes, 0xAB);
  return payload;
}

} // namespace

TEST(GetBlocks, RoundTripsALocator) {
  auto locator = locator_of(20);

  auto decoded = deserialize_getblocks(serialize_getblocks(locator));

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, locator);
}

TEST(GetBlocks, PreservesOrder) {
  auto locator = locator_of(5);

  auto decoded = deserialize_getblocks(serialize_getblocks(locator));

  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->size(), 5u);
  for (size_t i = 0; i < locator.size(); i++) {
    EXPECT_EQ((*decoded)[i], locator[i]) << "at index " << i;
  }
}

TEST(GetBlocks, RoundTripsASingleHash) {
  auto locator = locator_of(1);

  auto decoded = deserialize_getblocks(serialize_getblocks(locator));

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, locator);
}

TEST(GetBlocks, RoundTripsAnEmptyLocator) {
  auto decoded = deserialize_getblocks(serialize_getblocks({}));

  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->empty());
}

TEST(GetBlocks, SerializedSizeIsCountPlusHashes) {
  EXPECT_EQ(serialize_getblocks(locator_of(7)).size(), 4u + 7u * 32u);
}

TEST(GetBlocks, AcceptsExactlyTheMaximumNumberOfHashes) {
  auto locator = locator_of(kMaxLocatorHashes);

  auto decoded = deserialize_getblocks(serialize_getblocks(locator));

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->size(), kMaxLocatorHashes);
}

TEST(GetBlocks, RejectsMoreHashesThanTheMaximum) {
  auto decoded =
      deserialize_getblocks(serialize_getblocks(locator_of(kMaxLocatorHashes + 1)));

  EXPECT_EQ(decoded, std::nullopt);
}

TEST(GetBlocks, RejectsAHugeCountWithoutAllocating) {
  EXPECT_EQ(deserialize_getblocks(payload_with_count(0xFFFFFFFF, 0)),
            std::nullopt);
}

TEST(GetBlocks, RejectsAnEmptyPayload) {
  EXPECT_EQ(deserialize_getblocks({}), std::nullopt);
}

TEST(GetBlocks, RejectsATruncatedCount) {
  EXPECT_EQ(deserialize_getblocks({0x01, 0x00}), std::nullopt);
}

TEST(GetBlocks, RejectsATruncatedHash) {
  EXPECT_EQ(deserialize_getblocks(payload_with_count(2, 63)), std::nullopt);
}

TEST(GetBlocks, RejectsTrailingBytes) {
  auto payload = serialize_getblocks(locator_of(3));
  payload.push_back(0x00);

  EXPECT_EQ(deserialize_getblocks(payload), std::nullopt);
}

TEST(GetBlocks, RejectsMoreHashesThanTheCountClaims) {
  EXPECT_EQ(deserialize_getblocks(payload_with_count(1, 64)), std::nullopt);
}
