#include "chain/ChainManager.hpp"
#include "consensus/ConsensusParams.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "storage/Storage.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <unistd.h>
#include <vector>
#include <utility>
using namespace forgechain::chain;
using namespace forgechain::core;
using namespace forgechain::crypto;
using forgechain::consensus::ConsensusParams;
using forgechain::consensus::mine_block;
using forgechain::consensus::mining_reward;
using forgechain::storage::Storage;

namespace {

constexpr size_t kMempoolSize = 1000;
constexpr uint64_t kNow = 1'700'000'000;
constexpr uint64_t kDrift = 120;
const char *const kMiner = "miner-address";

const ConsensusParams kParams{.initial_difficulty = 4,
                              .min_difficulty = 1,
                              .target_block_time = 10,
                              .retarget_interval = 20,
                              .mtp_window = 11,
                              .max_future_drift = kDrift};

struct Fixture {
  uint64_t now{kNow};
  ChainManager manager;

  explicit Fixture(Storage *storage = nullptr)
      : manager(kMempoolSize, kParams, storage, [this] { return now; }) {}
};

Block mine_on(const HashBytes &prev, uint64_t timestamp,
              std::vector<Transaction> txs = {}) {
  return mine_block(1, prev, timestamp, kParams.initial_difficulty,
                    std::move(txs));
}

Block mine_with_coinbase(const HashBytes &prev, uint64_t timestamp) {
  std::vector<Transaction> txs{
      Transaction{kCoinbaseSender, kMiner, mining_reward, bytes{}, 0}};
  return mine_on(prev, timestamp, std::move(txs));
}

std::string next_test_db_path() {
  static int counter = 0;
  return "/tmp/forgechain_chain_timestamp_" + std::to_string(getpid()) + "_" +
         std::to_string(counter++) + ".db";
}

struct TempDb {
  std::string path{next_test_db_path()};
  TempDb() = default;
  TempDb(const TempDb &) = delete;
  TempDb &operator=(const TempDb &) = delete;
  ~TempDb() { std::remove(path.c_str()); }
};

} // namespace

TEST(ChainManagerTimestamp, AcceptsBlockWithValidTimestamp) {
  Fixture f;
  Block block = mine_on(f.manager.latest_hash(), kNow);

  auto outcome = f.manager.submit_block(block);

  EXPECT_EQ(outcome.status, BlockOutcome::Status::Accepted);
  EXPECT_EQ(f.manager.chain_height(), 2u);
}

TEST(ChainManagerTimestamp, AcceptsBlockExactlyAtDrift) {
  Fixture f;
  Block block = mine_on(f.manager.latest_hash(), kNow + kDrift);

  EXPECT_EQ(f.manager.submit_block(block).status,
            BlockOutcome::Status::Accepted);
}

TEST(ChainManagerTimestamp, RejectsBlockFromFuture) {
  Fixture f;
  Block block = mine_on(f.manager.latest_hash(), kNow + kDrift + 1);

  auto outcome = f.manager.submit_block(block);

  EXPECT_EQ(outcome.status, BlockOutcome::Status::Rejected);
  EXPECT_TRUE(outcome.to_broadcast.empty());
  EXPECT_EQ(f.manager.chain_height(), 1u);
  EXPECT_FALSE(f.manager.has_block(block.hash_));
}

TEST(ChainManagerTimestamp, RejectedFutureBlockDoesNotPayCoinbase) {
  Fixture f;
  Block block = mine_with_coinbase(f.manager.latest_hash(), kNow + kDrift + 1);

  f.manager.submit_block(block);

  EXPECT_FALSE(f.manager.get_balance(kMiner).has_value());
}

TEST(ChainManagerTimestamp, SameBlockAcceptedOnceClockCatchesUp) {
  Fixture f;
  Block block = mine_with_coinbase(f.manager.latest_hash(), kNow + kDrift + 60);

  EXPECT_EQ(f.manager.submit_block(block).status,
            BlockOutcome::Status::Rejected);

  f.now += 60;

  EXPECT_EQ(f.manager.submit_block(block).status,
            BlockOutcome::Status::Accepted);
  EXPECT_EQ(f.manager.chain_height(), 2u);
  EXPECT_EQ(f.manager.get_balance(kMiner), mining_reward);
}

TEST(ChainManagerTimestamp, RejectsTimestampEqualToMtp) {
  Fixture f;
  Block b1 = mine_on(f.manager.latest_hash(), kNow - 100);
  ASSERT_EQ(f.manager.submit_block(b1).status, BlockOutcome::Status::Accepted);
  Block b2 = mine_on(b1.hash_, kNow - 50);
  ASSERT_EQ(f.manager.submit_block(b2).status, BlockOutcome::Status::Accepted);

  Block at_mtp = mine_on(b2.hash_, kNow - 100);
  EXPECT_EQ(f.manager.submit_block(at_mtp).status,
            BlockOutcome::Status::Rejected);
  EXPECT_EQ(f.manager.chain_height(), 3u);

  Block above_mtp = mine_on(b2.hash_, kNow - 99);
  EXPECT_EQ(f.manager.submit_block(above_mtp).status,
            BlockOutcome::Status::Accepted);
  EXPECT_EQ(f.manager.chain_height(), 4u);
}

TEST(ChainManagerTimestamp, AcceptsTimestampOlderThanTipButAboveMtp) {
  Fixture f;
  Block b1 = mine_on(f.manager.latest_hash(), kNow - 100);
  ASSERT_EQ(f.manager.submit_block(b1).status, BlockOutcome::Status::Accepted);
  Block b2 = mine_on(b1.hash_, kNow);
  ASSERT_EQ(f.manager.submit_block(b2).status, BlockOutcome::Status::Accepted);

  Block b3 = mine_on(b2.hash_, kNow - 10);
  EXPECT_EQ(f.manager.submit_block(b3).status, BlockOutcome::Status::Accepted);
}

TEST(ChainManagerTimestamp, ManyBlocksInOneSecondEventuallyRejected) {
  Fixture f;
  HashBytes prev = f.manager.latest_hash();
  size_t accepted = 0;
  for (int i = 0; i < 5; i++) {
    Block block = mine_on(prev, kNow);
    if (f.manager.submit_block(block).status != BlockOutcome::Status::Accepted)
      break;
    prev = block.hash_;
    accepted++;
  }
  EXPECT_EQ(accepted, 1u);
}

TEST(ChainManagerTimestamp, RejectedBlockIsNotPersisted) {
  TempDb db;
  Storage storage(db.path);
  Fixture f(&storage);

  Block block = mine_on(f.manager.latest_hash(), kNow + kDrift + 1);
  f.manager.submit_block(block);

  EXPECT_EQ(storage.block_count(), 1u);
}

TEST(ChainManagerTimestamp, DefaultClockIsWallTime) {
  ChainManager manager(kMempoolSize, kParams);
  auto wall_now = static_cast<uint64_t>(std::time(nullptr));

  Block future = mine_on(manager.latest_hash(), wall_now + 3600);
  EXPECT_EQ(manager.submit_block(future).status,
            BlockOutcome::Status::Rejected);

  Block present = mine_on(manager.latest_hash(), wall_now);
  EXPECT_EQ(manager.submit_block(present).status,
            BlockOutcome::Status::Accepted);
}
