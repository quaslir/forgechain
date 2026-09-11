#include "chain/ChainManager.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "storage/Storage.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

using namespace forgechain::chain;
using namespace forgechain::core;
using namespace forgechain::crypto;
using forgechain::consensus::mine_block;
using forgechain::storage::Storage;

namespace {

constexpr size_t kMempoolSize = 1000;

constexpr uint32_t kLightDifficulty = 4;
constexpr uint32_t kHeavyDifficulty = 8;

std::string next_test_db_path() {
  static int counter = 0;
  return "/tmp/forgechain_chain_persistence_" + std::to_string(getpid()) + "_" +
         std::to_string(counter++) + ".db";
}

struct TempDb {
  std::string path{next_test_db_path()};
  TempDb() = default;
  TempDb(const TempDb &) = delete;
  TempDb &operator=(const TempDb &) = delete;
  ~TempDb() { std::remove(path.c_str()); }
};

Block mine_on(const HashBytes &prev_hash, uint64_t timestamp,
              uint32_t difficulty) {
  return mine_block(1, prev_hash, timestamp, difficulty,
                    std::vector<Transaction>{});
}

std::vector<HashBytes> hashes_on_disk(const std::string &path) {
  Storage reader(path);
  std::vector<HashBytes> result;
  size_t count = reader.block_count();
  for (size_t i = 0; i < count; i++) {
    auto block = reader.load_block(i);
    if (!block.has_value())
      break;
    result.push_back(block->hash_);
  }
  return result;
}

} // namespace

TEST(ChainPersistence, GenesisIsWrittenWhenDatabaseIsEmpty) {
  TempDb db;
  Storage storage(db.path);
  ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, &storage);

  EXPECT_EQ(storage.block_count(), 1u);
  auto genesis = storage.load_block(0);
  ASSERT_TRUE(genesis.has_value());
  EXPECT_EQ(genesis->hash_, manager.block_at(0).hash_);
}

TEST(ChainPersistence, AcceptedBlockIsWrittenAtItsOwnHeight) {
  TempDb db;
  Storage storage(db.path);
  ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, &storage);

  Block first = mine_on(manager.latest_hash(), 1000, kLightDifficulty);
  ASSERT_EQ(manager.submit_block(first).status, BlockOutcome::Status::Accepted);

  ASSERT_EQ(storage.block_count(), 2u);
  auto stored = storage.load_block(1);
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->hash_, first.hash_);
}

TEST(ChainPersistence, RejectedBlockIsNotWritten) {
  TempDb db;
  Storage storage(db.path);
  ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, &storage);

  HashBytes bogus_prev{};
  bogus_prev.fill(0x7f);
  Block orphaned = mine_on(bogus_prev, 1000, kLightDifficulty);
  auto outcome = manager.submit_block(orphaned);

  EXPECT_NE(outcome.status, BlockOutcome::Status::Accepted);
  EXPECT_EQ(storage.block_count(), 1u);
}

TEST(ChainPersistence, HeightsStayContiguousAcrossManyAppends) {
  TempDb db;
  Storage storage(db.path);
  ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, &storage);

  for (int i = 0; i < 10; i++) {
    Block b = mine_on(manager.latest_hash(), 1000 + static_cast<uint64_t>(i),
                      kLightDifficulty);
    ASSERT_EQ(manager.submit_block(b).status, BlockOutcome::Status::Accepted);
  }

  ASSERT_EQ(storage.block_count(), 11u);
  for (size_t i = 0; i < 11; i++) {
    auto stored = storage.load_block(i);
    ASSERT_TRUE(stored.has_value()) << "gap at height " << i;
    EXPECT_EQ(stored->hash_, manager.block_at(i).hash_)
        << "disk and memory disagree at height " << i;
  }
}

TEST(ChainPersistence, ReorgReplacesLosingBranchOnDisk) {
  TempDb db;
  Storage storage(db.path);
  ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, &storage);

  const HashBytes genesis_hash = manager.latest_hash();

  Block losing = mine_on(genesis_hash, 1000, kLightDifficulty);
  ASSERT_EQ(manager.submit_block(losing).status, BlockOutcome::Status::Accepted);
  ASSERT_EQ(storage.block_count(), 2u);

  Block winning = mine_on(genesis_hash, 2000, kHeavyDifficulty);
  auto outcome = manager.submit_block(winning);
  ASSERT_EQ(outcome.status, BlockOutcome::Status::Reorged);

  ASSERT_EQ(manager.chain_height(), 2u);
  ASSERT_EQ(manager.latest_hash(), winning.hash_);

  auto disk = hashes_on_disk(db.path);
  ASSERT_EQ(disk.size(), 2u);
  EXPECT_EQ(disk[0], genesis_hash);
  EXPECT_EQ(disk[1], winning.hash_)
      << "database still holds the losing branch after a reorg";
}

TEST(ChainPersistence, ReorgToLongerBranchWritesEveryNewBlock) {
  TempDb db;
  Storage storage(db.path);
  ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, &storage);

  const HashBytes genesis_hash = manager.latest_hash();

  Block losing = mine_on(genesis_hash, 1000, kHeavyDifficulty);
  ASSERT_EQ(manager.submit_block(losing).status, BlockOutcome::Status::Accepted);

  Block win1 = mine_on(genesis_hash, 2000, kHeavyDifficulty);
  Block win2 = mine_on(win1.hash_, 2001, kHeavyDifficulty);

  manager.submit_block(win1);
  auto outcome = manager.submit_block(win2);
  ASSERT_EQ(outcome.status, BlockOutcome::Status::Reorged);
  ASSERT_EQ(manager.chain_height(), 3u);

  auto disk = hashes_on_disk(db.path);
  ASSERT_EQ(disk.size(), 3u) << "new branch was not fully written";
  EXPECT_EQ(disk[0], genesis_hash);
  EXPECT_EQ(disk[1], win1.hash_);
  EXPECT_EQ(disk[2], win2.hash_);
}

TEST(ChainPersistence, DiskShrinksWhenWinningBranchIsShorter) {
  TempDb db;
  Storage storage(db.path);
  ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, &storage);

  const HashBytes genesis_hash = manager.latest_hash();

  Block losing1 = mine_on(genesis_hash, 1000, kLightDifficulty);
  ASSERT_EQ(manager.submit_block(losing1).status,
            BlockOutcome::Status::Accepted);
  Block losing2 = mine_on(losing1.hash_, 1001, kLightDifficulty);
  ASSERT_EQ(manager.submit_block(losing2).status,
            BlockOutcome::Status::Accepted);
  Block winning = mine_on(genesis_hash, 2000, kHeavyDifficulty);
  auto outcome = manager.submit_block(winning);
  ASSERT_EQ(outcome.status, BlockOutcome::Status::Reorged);

  auto disk = hashes_on_disk(db.path);
  ASSERT_EQ(disk.size(), manager.chain_height())
      << "stale heights left behind after switching to a shorter branch";
  for (size_t i = 0; i < disk.size(); i++) {
    EXPECT_EQ(disk[i], manager.block_at(i).hash_)
        << "disk and memory disagree at height " << i;
  }
}

TEST(ChainPersistence, ManagerWithoutStorageDoesNotCrash) {
  ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, nullptr);

  Block first = mine_on(manager.latest_hash(), 1000, kLightDifficulty);
  EXPECT_EQ(manager.submit_block(first).status, BlockOutcome::Status::Accepted);
  EXPECT_EQ(manager.chain_height(), 2u);
}

TEST(ChainPersistence, ExistingDatabaseKeepsItsGenesis) {
  TempDb db;
  HashBytes original_genesis{};

  {
    Storage storage(db.path);
    ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, &storage);
    original_genesis = manager.block_at(0).hash_;

    Block first = mine_on(manager.latest_hash(), 1000, kLightDifficulty);
    ASSERT_EQ(manager.submit_block(first).status,
              BlockOutcome::Status::Accepted);
  }

  {
    Storage storage(db.path);
    ChainManager manager(kMempoolSize, forgechain::consensus::kTestParams, &storage);
    auto genesis = storage.load_block(0);
    ASSERT_TRUE(genesis.has_value());
    EXPECT_EQ(genesis->hash_, original_genesis);
    EXPECT_EQ(storage.block_count(), 2u)
        << "constructing a manager on a populated database changed it";
  }
}
