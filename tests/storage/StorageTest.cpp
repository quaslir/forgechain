#include "storage/Storage.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

using namespace forgechain::storage;
using namespace forgechain::core;
using forgechain::crypto::HashBytes;

namespace {

class TempDbPath {
public:
  TempDbPath()
      : path_("/tmp/forgechain_storage_test_" +
              std::to_string(reinterpret_cast<uintptr_t>(this)) + ".db") {
    std::remove(path_.c_str());
  }
  ~TempDbPath() { std::remove(path_.c_str()); }

  TempDbPath(const TempDbPath &) = delete;
  TempDbPath &operator=(const TempDbPath &) = delete;

  [[nodiscard]] const std::string &path() const { return path_; }

private:
  std::string path_;
};

Block make_block(const HashBytes &prev_hash, uint64_t timestamp_seed,
                  std::vector<Transaction> txs) {
  Block b(1, prev_hash, timestamp_seed, std::move(txs));
  b.hash_ = b.compute_hash();
  return b;
}

}  // namespace

TEST(Storage, OpeningFreshPathCreatesUsableDatabase) {
  TempDbPath db;
  Storage storage(db.path());
  EXPECT_EQ(storage.block_count(), 0u);
}

TEST(Storage, OpeningNonExistentDirectoryThrows) {
  EXPECT_THROW(Storage("/this/directory/does/not/exist/db.sqlite"),
               std::runtime_error);
}

TEST(Storage, ReopeningExistingDatabaseDoesNotThrow) {
  TempDbPath db;
  { Storage storage(db.path()); }
  EXPECT_NO_THROW({ Storage storage(db.path()); });
}

TEST(Storage, SaveThenLoadBlockRoundTripsExactly) {
  TempDbPath db;
  Storage storage(db.path());

  Transaction tx("alice", "bob", 100, {}, 5);
  Block block = make_block(HashBytes{}, 1700000000, {tx});

  storage.save_block(block, 0);
  auto loaded = storage.load_block(0);

  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->hash_, block.hash_);
  EXPECT_EQ(loaded->prev_hash_, block.prev_hash_);
  EXPECT_EQ(loaded->timestamp_, block.timestamp_);
  ASSERT_EQ(loaded->transactions_.size(), 1u);
  EXPECT_EQ(loaded->transactions_[0].sender_, "alice");
  EXPECT_EQ(loaded->transactions_[0].recipient_, "bob");
  EXPECT_EQ(loaded->transactions_[0].amount_, 100u);
  EXPECT_EQ(loaded->transactions_[0].fee_, 5u);
}

TEST(Storage, LoadBlockAtUnknownHeightReturnsNullopt) {
  TempDbPath db;
  Storage storage(db.path());

  EXPECT_FALSE(storage.load_block(42).has_value());
}

TEST(Storage, LoadBlockOnEmptyDatabaseReturnsNullopt) {
  TempDbPath db;
  Storage storage(db.path());

  EXPECT_FALSE(storage.load_block(0).has_value());
}

TEST(Storage, BlockCountReflectsNumberOfSavedBlocks) {
  TempDbPath db;
  Storage storage(db.path());

  EXPECT_EQ(storage.block_count(), 0u);

  storage.save_block(make_block(HashBytes{}, 1000, {}), 0);
  EXPECT_EQ(storage.block_count(), 1u);

  storage.save_block(make_block(HashBytes{}, 1100, {}), 1);
  storage.save_block(make_block(HashBytes{}, 1200, {}), 2);
  EXPECT_EQ(storage.block_count(), 3u);
}

TEST(Storage, MultipleBlocksAreEachRetrievableByTheirOwnHeight) {
  TempDbPath db;
  Storage storage(db.path());

  Block genesis = make_block(HashBytes{}, 1000, {});
  Block second = make_block(genesis.hash_, 1100, {});
  Block third = make_block(second.hash_, 1200, {});

  storage.save_block(genesis, 0);
  storage.save_block(second, 1);
  storage.save_block(third, 2);

  auto loaded0 = storage.load_block(0);
  auto loaded1 = storage.load_block(1);
  auto loaded2 = storage.load_block(2);

  ASSERT_TRUE(loaded0.has_value());
  ASSERT_TRUE(loaded1.has_value());
  ASSERT_TRUE(loaded2.has_value());
  EXPECT_EQ(loaded0->hash_, genesis.hash_);
  EXPECT_EQ(loaded1->hash_, second.hash_);
  EXPECT_EQ(loaded2->hash_, third.hash_);
}

TEST(Storage, BlockWithNoTransactionsRoundTrips) {
  TempDbPath db;
  Storage storage(db.path());

  Block empty_block = make_block(HashBytes{}, 1000, {});
  storage.save_block(empty_block, 0);

  auto loaded = storage.load_block(0);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_TRUE(loaded->transactions_.empty());
}

TEST(Storage, BlockWithMultipleTransactionsRoundTrips) {
  TempDbPath db;
  Storage storage(db.path());

  Transaction tx1("alice", "bob", 100, {}, 5);
  Transaction tx2("bob", "carol", 30, {}, 2);
  Block block = make_block(HashBytes{}, 1000, {tx1, tx2});
  storage.save_block(block, 0);

  auto loaded = storage.load_block(0);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_EQ(loaded->transactions_.size(), 2u);
  EXPECT_EQ(loaded->transactions_[0].sender_, "alice");
  EXPECT_EQ(loaded->transactions_[1].sender_, "bob");
}

TEST(Storage, DataPersistsAcrossStorageCloseAndReopen) {
  TempDbPath db;

  {
    Storage storage(db.path());
    storage.save_block(make_block(HashBytes{}, 1000, {}), 0);
    storage.save_balance("alice", 895);
  }

  {
    Storage storage(db.path());
    EXPECT_EQ(storage.block_count(), 1u);
    EXPECT_TRUE(storage.load_block(0).has_value());
    EXPECT_EQ(storage.load_balance("alice"), std::optional<uint64_t>(895));
  }
}

TEST(Storage, SaveThenLoadBalanceRoundTrips) {
  TempDbPath db;
  Storage storage(db.path());

  storage.save_balance("alice-address", 1000);
  EXPECT_EQ(storage.load_balance("alice-address"), std::optional<uint64_t>(1000));
}

TEST(Storage, LoadBalanceForUnknownAddressReturnsNullopt) {
  TempDbPath db;
  Storage storage(db.path());

  EXPECT_FALSE(storage.load_balance("nobody").has_value());
}

TEST(Storage, SaveBalanceOnExistingAddressReplacesNotDuplicates) {
  TempDbPath db;
  Storage storage(db.path());

  storage.save_balance("alice", 1000);
  storage.save_balance("alice", 500);

  EXPECT_EQ(storage.load_balance("alice"), std::optional<uint64_t>(500));
  EXPECT_EQ(storage.load_all_balances().size(), 1u);
}

TEST(Storage, SaveBalanceOfZeroIsDistinctFromNoBalance) {
  TempDbPath db;
  Storage storage(db.path());

  storage.save_balance("alice", 0);

  auto balance = storage.load_balance("alice");
  ASSERT_TRUE(balance.has_value());
  EXPECT_EQ(*balance, 0u);
}

TEST(Storage, LoadAllBalancesReturnsEmptyOnFreshDatabase) {
  TempDbPath db;
  Storage storage(db.path());

  EXPECT_TRUE(storage.load_all_balances().empty());
}

TEST(Storage, LoadAllBalancesReturnsEveryStoredAddress) {
  TempDbPath db;
  Storage storage(db.path());

  storage.save_balance("alice", 100);
  storage.save_balance("bob", 200);
  storage.save_balance("carol", 300);

  auto all = storage.load_all_balances();
  ASSERT_EQ(all.size(), 3u);

  std::optional<uint64_t> alice, bob, carol;
  for (const auto &[address, amount] : all) {
    if (address == "alice") alice = amount;
    if (address == "bob") bob = amount;
    if (address == "carol") carol = amount;
  }
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(carol.has_value());
  EXPECT_EQ(*alice, 100u);
  EXPECT_EQ(*bob, 200u);
  EXPECT_EQ(*carol, 300u);
}

TEST(Storage, BlocksAndBalancesAreIndependentOfEachOther) {
  TempDbPath db;
  Storage storage(db.path());

  storage.save_block(make_block(HashBytes{}, 1000, {}), 0);
  storage.save_balance("alice", 100);

  EXPECT_EQ(storage.block_count(), 1u);
  EXPECT_EQ(storage.load_all_balances().size(), 1u);

  storage.save_block(make_block(HashBytes{}, 1100, {}), 1);
  EXPECT_EQ(storage.load_all_balances().size(), 1u);
}
